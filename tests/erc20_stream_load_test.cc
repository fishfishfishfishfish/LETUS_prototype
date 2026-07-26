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
  uint64_t num_accounts = 1000;
  double contract_ratio = 0.1;
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
      << "  --num-accounts N          account-space size (default: 1000)\n"
      << "  --contract-ratio R        contract ratio in (0,1) (default: 0.1)\n"
      << "  --base-data N             balances to load (default: 10000)\n"
      << "  --load-batch-size N       puts per commit (default: 1000)\n"
      << "  --initial-balance-max N   maximum initial balance (default: 1000000)\n"
      << "  --seed N                  deterministic seed (default: 42)\n"
      << "  --key-len N               composite key length (default: 32)\n"
      << "  --verify-samples N        loaded balances to read back (default: 1000)\n"
      << "  --data-dir PATH           value-store directory\n"
      << "  --index-dir PATH          page-store directory\n"
      << "  --keep-data               preserve generated files\n";
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
    if (arg == "--num-accounts") options.num_accounts = ParseU64(value, arg);
    else if (arg == "--contract-ratio") {
      try {
        size_t consumed = 0;
        options.contract_ratio = std::stod(value, &consumed);
        if (consumed != std::string(value).size())
          throw std::invalid_argument("");
      } catch (...) {
        Usage(argv[0], "invalid value for " + arg + ": " + value);
      }
    } else if (arg == "--base-data") options.base_data = ParseU64(value, arg);
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
  if (options.num_accounts == 0 || options.base_data == 0 ||
      options.load_batch_size == 0 || options.key_len <= 0 ||
      options.contract_ratio <= 0.0 || options.contract_ratio >= 1.0) {
    Usage(argv[0], "numeric sizes must be positive and contract-ratio in (0,1)");
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

std::string MakeErcKey(const std::string& contract,
                       const std::string& holder, int key_len) {
  std::string key = contract + holder;
  key.append(static_cast<size_t>(
                 std::max(0, key_len - static_cast<int>(key.size()))),
             '0');
  return key;
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

  const int num_contracts = std::max(
      1, static_cast<int>(options.contract_ratio * options.num_accounts));
  const uint64_t holders_per_contract =
      std::max<uint64_t>(1, options.base_data / num_contracts);
  const uint64_t holder_base = static_cast<uint64_t>(num_contracts);
  const uint64_t expected_writes =
      static_cast<uint64_t>(num_contracts) * holders_per_contract;
  if (holders_per_contract < 2) {
    Usage(argv[0],
          "erc20_transfer.bak requires at least two holders per contract");
  }

  std::cout << "num_contracts=" << num_contracts
            << " holders_per_contract=" << holders_per_contract
            << " base_data=" << options.base_data
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
    CounterGenerator holder_generator(
        holder_base, holder_base + holders_per_contract - 1);

    uint64_t version = 1;
    size_t batch_count = 0;
    size_t commit_count = 0;
    std::unordered_map<std::string, std::string> expected;
    expected.reserve(static_cast<size_t>(expected_writes));

    // This loop intentionally mirrors erc20_transfer.bak's STREAM LOAD PHASE.
    for (int contract_index = 0; contract_index < num_contracts;
         ++contract_index) {
      holder_generator.Set(holder_base + holders_per_contract - 1);
      const std::string contract =
          DeriveAddress(static_cast<uint64_t>(contract_index), options.key_len);
      for (uint64_t k = 0; k < holders_per_contract; ++k) {
        const std::string holder =
            DeriveAddress(holder_generator.Next(), options.key_len);
        const std::string key = MakeErcKey(contract, holder, options.key_len);
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
    }
    if (batch_count > 0) {
      trie->Commit(version++);
      ++commit_count;
    }

    const size_t expected_commits = static_cast<size_t>(
        (expected_writes + options.load_batch_size - 1) /
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

    std::cout << "PASS: loaded=" << expected_writes
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
