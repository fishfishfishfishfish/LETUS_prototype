#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "DMMTrie.hpp"
#include "LSVPS.hpp"
#include "VDLS.hpp"

namespace fs = std::filesystem;

struct Options {
  uint64_t seed = 42;
  size_t versions = 80;
  size_t writes_per_version = 4000;
  size_t key_bytes = 32;
  size_t value_bytes = 64;
  size_t verify_samples = 1000;
  size_t hot_key_count = 0;
  std::string data_dir;
  std::string index_dir;
  bool keep_data = false;
};

[[noreturn]] void Usage(const char* program, const std::string& error = "") {
  if (!error.empty()) std::cerr << "error: " << error << "\n";
  std::cerr
      << "Usage: " << program << " [options]\n"
      << "  --seed N                deterministic PRNG seed (default: 42)\n"
      << "  --versions N            number of commits (default: 80)\n"
      << "  --writes-per-version N  writes in each commit (default: 4000)\n"
      << "  --key-bytes N           random key length in bytes (default: 32)\n"
      << "  --value-bytes N         value length in bytes (default: 64)\n"
      << "  --verify-samples N      latest values checked (default: 1000)\n"
      << "  --hot-key-count N       reuse N random keys; 0 means all keys random\n"
      << "  --data-dir PATH         value-store directory\n"
      << "  --index-dir PATH        page-store directory\n"
      << "  --keep-data             do not remove generated data\n";
  std::exit(error.empty() ? 0 : 2);
}

size_t ParseSize(const char* text, const char* option) {
  try {
    size_t consumed = 0;
    const auto result = std::stoull(text, &consumed);
    if (consumed != std::string(text).size()) throw std::invalid_argument("");
    return static_cast<size_t>(result);
  } catch (...) {
    Usage("random_key_write_test",
          std::string("invalid value for ") + option + ": " + text);
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
    if (arg == "--seed") options.seed = ParseSize(value, arg.c_str());
    else if (arg == "--versions") options.versions = ParseSize(value, arg.c_str());
    else if (arg == "--writes-per-version")
      options.writes_per_version = ParseSize(value, arg.c_str());
    else if (arg == "--key-bytes")
      options.key_bytes = ParseSize(value, arg.c_str());
    else if (arg == "--value-bytes")
      options.value_bytes = ParseSize(value, arg.c_str());
    else if (arg == "--verify-samples")
      options.verify_samples = ParseSize(value, arg.c_str());
    else if (arg == "--hot-key-count")
      options.hot_key_count = ParseSize(value, arg.c_str());
    else if (arg == "--data-dir") options.data_dir = value;
    else if (arg == "--index-dir") options.index_dir = value;
    else Usage(argv[0], "unknown option: " + arg);
  }
  if (options.versions == 0 || options.writes_per_version == 0 ||
      options.key_bytes == 0 || options.value_bytes == 0) {
    Usage(argv[0], "versions, writes and key/value sizes must be positive");
  }
  return options;
}

std::string RandomHexKey(std::mt19937_64& rng, size_t bytes) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string key(bytes * 2, '0');
  for (char& c : key) c = hex[rng() & 0xf];
  return key;
}

std::string MakeValue(uint64_t version, size_t write_index, size_t bytes) {
  std::ostringstream prefix;
  prefix << "v" << version << "-i" << write_index << "-";
  std::string value = prefix.str();
  if (value.size() < bytes) value.append(bytes - value.size(), 'x');
  value.resize(bytes);
  return value;
}

int main(int argc, char** argv) {
  const Options options = ParseOptions(argc, argv);
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path base =
      fs::temp_directory_path() /
      ("letus-random-key-" + std::to_string(stamp));
  const fs::path data_dir =
      options.data_dir.empty() ? base / "data" : fs::path(options.data_dir);
  const fs::path index_dir =
      options.index_dir.empty() ? base / "index" : fs::path(options.index_dir);
  fs::create_directories(data_dir);
  fs::create_directories(index_dir);

  std::cout << "seed=" << options.seed << " versions=" << options.versions
            << " writes_per_version=" << options.writes_per_version
            << " key_bytes=" << options.key_bytes
            << " hot_key_count=" << options.hot_key_count << "\n";

  std::mt19937_64 rng(options.seed);
  std::vector<std::string> hot_keys;
  for (size_t i = 0; i < options.hot_key_count; ++i)
    hot_keys.push_back(RandomHexKey(rng, options.key_bytes));
  std::unordered_map<std::string, std::string> expected;

  try {
    auto page_store = std::make_unique<LSVPS>(index_dir.string());
    auto value_store =
        std::make_unique<VDLS>((data_dir.string() + fs::path::preferred_separator));
    auto trie =
        std::make_unique<DMMTrie>(0, page_store.get(), value_store.get());
    page_store->RegisterTrie(trie.get());

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t version = 1; version <= options.versions; ++version) {
      for (size_t i = 0; i < options.writes_per_version; ++i) {
        const std::string key =
            hot_keys.empty() ? RandomHexKey(rng, options.key_bytes)
                             : hot_keys[rng() % hot_keys.size()];
        const std::string value = MakeValue(version, i, options.value_bytes);
        if (!trie->Put(0, version, key, value)) {
          throw std::runtime_error("Put returned false at version " +
                                   std::to_string(version));
        }
        expected[key] = value;
      }
      trie->Commit(version);
      if (version == 1 || version % 10 == 0 ||
          version == options.versions) {
        std::cout << "committed version " << version << " ("
                  << version * options.writes_per_version << " writes)\n";
      }
    }

    std::vector<std::string> keys;
    keys.reserve(expected.size());
    for (const auto& item : expected) keys.push_back(item.first);
    std::shuffle(keys.begin(), keys.end(), rng);
    const size_t checks = std::min(options.verify_samples, keys.size());
    for (size_t i = 0; i < checks; ++i) {
      const std::string actual =
          trie->Get(0, options.versions, keys[i]);
      if (actual != expected.at(keys[i])) {
        std::cerr << "value mismatch for key " << keys[i]
                  << " at latest version " << options.versions << "\n";
        return 1;
      }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);
    std::cout << "PASS: " << options.versions * options.writes_per_version
              << " writes, " << checks << " verified, " << std::fixed
              << std::setprecision(2) << elapsed.count() << " seconds\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    std::cerr << "data=" << data_dir << " index=" << index_dir << "\n";
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
