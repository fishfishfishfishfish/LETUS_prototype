#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "DMMTrie.hpp"
#include "LSVPS.hpp"
#include "VDLS.hpp"
#include "generator.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
  uint64_t base_data = 10000;
  size_t load_batch_size = 1000;
  uint64_t initial_balance_max = 1000000;
  uint64_t seed = 42;
  int key_len = 32;
  size_t verify_samples = 1000;
  std::string data_dir;
  std::string index_dir;
  bool keep_data = false;
};

[[noreturn]] void Usage(const char* program, const std::string& error = "") {
  if (!error.empty()) std::cerr << "error: " << error << "\n";
  std::cerr
      << "Usage: " << program << " [options]\n"
      << "  --base-data N            balances to load (default: 10000)\n"
      << "  --load-batch-size N      puts per commit (default: 1000)\n"
      << "  --initial-balance-max N  maximum initial balance (default: 1000000)\n"
      << "  --seed N                 deterministic seed (default: 42)\n"
      << "  --key-len N              account key length (default: 32)\n"
      << "  --verify-samples N       loaded balances to read back (default: 1000)\n"
      << "  --data-dir PATH          value-store directory\n"
      << "  --index-dir PATH         page-store directory\n"
      << "  --keep-data              preserve generated files\n";
  std::exit(error.empty() ? 0 : 2);
}

uint64_t ParseU64(const char* value, const std::string& option) {
  try {
    size_t consumed = 0;
    const uint64_t parsed = std::stoull(value, &consumed);
    if (consumed != std::string(value).size()) throw std::invalid_argument("");
    return parsed;
  } catch (...) {
    Usage("erc20_stream_load_test", "invalid value for " + option + ": " + value);
  }
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") Usage(argv[0]);
    if (arg == "--keep-data") {
      options.keep_data = true;
      continue;
    }
    if (i + 1 >= argc) Usage(argv[0], "missing value for " + arg);
    const char* value = argv[++i];
    if (arg == "--base-data") options.base_data = ParseU64(value, arg);
    else if (arg == "--load-batch-size")
      options.load_batch_size = static_cast<size_t>(ParseU64(value, arg));
    else if (arg == "--initial-balance-max")
      options.initial_balance_max = ParseU64(value, arg);
    else if (arg == "--seed") options.seed = ParseU64(value, arg);
    else if (arg == "--key-len")
      options.key_len = static_cast<int>(ParseU64(value, arg));
    else if (arg == "--verify-samples")
      options.verify_samples = static_cast<size_t>(ParseU64(value, arg));
    else if (arg == "--data-dir") options.data_dir = value;
    else if (arg == "--index-dir") options.index_dir = value;
    else Usage(argv[0], "unknown option: " + arg);
  }
  if (options.base_data == 0 || options.load_batch_size == 0 ||
      options.key_len <= 0) {
    Usage(argv[0], "base-data, load-batch-size and key-len must be positive");
  }
  return options;
}

std::string EncodeU64LE(uint64_t value) {
  std::string encoded(8, '\0');
  for (int i = 0; i < 8; ++i)
    encoded[i] = static_cast<char>((value >> (8 * i)) & 0xff);
  return encoded;
}

std::string DeriveAddress(uint64_t account, int key_len) {
  const std::string number = std::to_string(account);
  const int zeros = std::max(0, key_len / 2 - static_cast<int>(number.size()));
  return std::string(static_cast<size_t>(zeros), '0') + number;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = ParseOptions(argc, argv);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path base = fs::temp_directory_path() /
                        ("letus-erc20-stream-load-" + std::to_string(stamp));
  const fs::path data_dir =
      options.data_dir.empty() ? base / "data" : fs::path(options.data_dir);
  const fs::path index_dir =
      options.index_dir.empty() ? base / "index" : fs::path(options.index_dir);
  fs::create_directories(data_dir);
  fs::create_directories(index_dir);

  std::cout << "base_data=" << options.base_data
            << " load_batch_size=" << options.load_batch_size
            << " seed=" << options.seed << " key_len=" << options.key_len
            << "\n";

  try {
    auto page_store = std::make_unique<LSVPS>(index_dir.string());
    auto value_store = std::make_unique<VDLS>(
        data_dir.string() + fs::path::preferred_separator);
    auto trie = std::make_unique<DMMTrie>(0, page_store.get(), value_store.get());
    page_store->RegisterTrie(trie.get());

    UniformGenerator balance_generator(0, options.initial_balance_max,
                                       options.seed + 1);
    CounterGenerator account_generator(0, options.base_data);
    // Keep this identical to erc20_transfer.cc's STREAM LOAD PHASE.
    account_generator.Set(options.base_data - 1);

    uint64_t version = 1;
    size_t batch_count = 0;
    size_t commit_count = 0;
    std::unordered_map<std::string, std::string> expected;
    expected.reserve(static_cast<size_t>(options.base_data));

    for (uint64_t i = 0; i < options.base_data; ++i) {
      const std::string key =
          DeriveAddress(account_generator.Next(), options.key_len);
      const std::string value = EncodeU64LE(balance_generator.Next());
      if (!trie->Put(0, version, key, value))
        throw std::runtime_error("Put returned false at version " +
                                 std::to_string(version));
      expected[key] = value;
      ++batch_count;
      if (batch_count >= options.load_batch_size) {
        trie->Commit(version++);
        ++commit_count;
        batch_count = 0;
      }
    }
    if (batch_count > 0) {
      trie->Commit(version++);
      ++commit_count;
    }

    const size_t expected_commits = static_cast<size_t>(
        (options.base_data + options.load_batch_size - 1) /
        options.load_batch_size);
    if (commit_count != expected_commits)
      throw std::runtime_error("commit count mismatch: expected " +
                               std::to_string(expected_commits) + ", got " +
                               std::to_string(commit_count));

    std::vector<std::string> keys;
    keys.reserve(expected.size());
    for (const auto& entry : expected) keys.push_back(entry.first);
    std::mt19937_64 sample_rng(options.seed + 2);
    std::shuffle(keys.begin(), keys.end(), sample_rng);
    const size_t checks = std::min(options.verify_samples, keys.size());
    const uint64_t latest_version = version - 1;
    for (size_t i = 0; i < checks; ++i) {
      const std::string actual = trie->Get(0, latest_version, keys[i]);
      if (actual != expected.at(keys[i]))
        throw std::runtime_error("balance mismatch for key " + keys[i]);
    }

    std::cout << "PASS: loaded=" << options.base_data
              << " commits=" << commit_count << " verified=" << checks
              << " latest_version=" << latest_version << "\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n"
              << "data=" << data_dir << " index=" << index_dir << "\n";
    return 1;
  }

  if (!options.keep_data && options.data_dir.empty() &&
      options.index_dir.empty()) {
    std::error_code ignored;
    fs::remove_all(base, ignored);
  } else {
    std::cout << "data=" << data_dir << " index=" << index_dir << "\n";
  }
  return 0;
}
