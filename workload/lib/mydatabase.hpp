#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "DMMTrie.hpp"
#include "LSVPS.hpp"
#include "generator.hpp"

// Generate random string with specified length (C++11 compatible)
std::string generate_random_string(int length) {
  const std::string chars =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(
      0, static_cast<int>(chars.size() - 1));

  std::string result;
  result.reserve(length);
  for (int i = 0; i < length; ++i) {
    result += chars[dis(gen)];
  }
  return result;
}

// Custom database interface (replace with your actual implementation)
class MyDatabase {
 public:
  // Constructor: Initialize database (create test table)
  MyDatabase(const std::string& db_path) : version(0) {
    std::string index_path = db_path + "/index";
    std::string data_path = db_path + "/data";
    // init database
    page_store = new LSVPS(index_path);
    value_store = new VDLS(data_path);
    trie = new DMMTrie(0, page_store, value_store);
    page_store->RegisterTrie(trie);
  }
  ~MyDatabase() {
    delete trie;
    delete value_store;
    delete page_store;
  }

  // Write base data (build test environment)
  bool write_base_data(uint64_t count, int key_len, int value_len,
                       uint64_t start_key = 1) {
    // Generate fixed value for consistency (same value for all records)
    std::string fixed_value = generate_random_string(value_len);

    const size_t BATCH_SIZE = 1000;  // Batch size for efficient writing
    size_t current_batch_size = 0;

    // Process data in batches for better performance
    for (uint64_t i = 0; i < count; ++i) {
      uint64_t key_num = start_key + i;

      // Generate fixed-length key (pad with 0 if needed)
      std::string key = std::to_string(key_num);
      if (key.size() < static_cast<size_t>(key_len)) {
        key = std::string(static_cast<size_t>(key_len) - key.size(), '0') + key;
      } else if (key.size() > static_cast<size_t>(key_len)) {
        key = key.substr(0, static_cast<size_t>(key_len));
      }

      trie->Put(0, version, key, fixed_value);
      current_batch_size++;

      // Process batch when batch size is reached or at the end
      if (current_batch_size >= BATCH_SIZE || i == count - 1) {
        trie->Commit(version);
        version++;
        current_batch_size = 0;
        if (version % 100 == 0) {
          std::cout << "write base data version: " << version << std::endl;
        }
      }
    }

    return true;
  }

  // Test read throughput (random read with dynamic batch size)
  bool test_batch_read(uint64_t total_base_count, int batch_size, int key_len,
                       double& throughput) {
    // Generate random keys for warm-up
    std::vector<std::string> warmup_keys =
        generate_random_keys(total_base_count, 100, key_len);
    read_data(warmup_keys);

    // Generate random keys for actual test
    std::vector<std::string> test_keys =
        generate_random_keys(total_base_count, batch_size, key_len);
    auto start = std::chrono::high_resolution_clock::now();
    read_data(test_keys);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    throughput =
        static_cast<double>(batch_size) / elapsed.count();  // records/second
    return true;
  }

  // Test write throughput (batch write with dynamic batch size)
  bool test_batch_write(uint64_t total_base_count, int batch_size, int key_len,
                        int value_len, double& throughput) {
    // Generate fixed value for consistency (same value for all records)
    std::string fixed_value = generate_random_string(value_len);
    // Generate random key:value pairs for write test
    std::vector<std::string> test_keys =
        generate_random_keys(total_base_count, batch_size, key_len);
    std::vector<std::string> test_values;
    test_values.reserve(batch_size);
    for (const auto& key : test_keys) {
      test_values.push_back(fixed_value);
    }

    auto start = std::chrono::high_resolution_clock::now();
    write_data(test_keys, test_values);
    version++;
    // Force flush to disk (critical for accurate IO measurement)
    flush();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    throughput =
        static_cast<double>(batch_size) / elapsed.count();  // records/second
    return true;
  }

  // Random read data (internal helper)
  void read_data(const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
      trie->Get(0, version - 1, key);
    }
  }

  // Write data with key:value pairs (internal helper)
  void write_data(const std::vector<std::string>& keys,
                  const std::vector<std::string>& values) {
    // Ensure keys and values have the same size
    if (keys.size() != values.size()) {
      std::cerr << "Error: keys and vectors size mismatch!" << std::endl;
      return;
    }
    for (int i = 0; i < keys.size(); i++) {
      trie->Put(0, version, keys[i], values[i]);
    }
    trie->Commit(version);
  }

  // Generate random keys (internal helper)
  std::vector<std::string> generate_random_keys(uint64_t total_base_count,
                                                int key_count, int key_len) {
    std::vector<std::string> keys;
    keys.reserve(key_count);

    ZipfianGenerator key_generator(
        1, total_base_count,
        0.99);  // Zipfian distribution for realistic access patterns

    for (int i = 0; i < key_count; ++i) {
      uint64_t id = total_base_count - key_generator.Next();
      // Generate fixed-length key
      std::string key = std::to_string(id);
      if (key.size() < static_cast<size_t>(key_len)) {
        key = std::string(static_cast<size_t>(key_len) - key.size(), '0') + key;
      } else if (key.size() > static_cast<size_t>(key_len)) {
        key = key.substr(0, static_cast<size_t>(key_len));
      }
      keys.push_back(key);
    }

    return keys;
  }

  // Force flush to disk (Linux: fsync/fdatasync)
  bool flush() {
    page_store->Flush();
    return true;
  }

  // Clear all test data (ensure clean environment for each scale)
  bool clear_all_data() {
    // Replace with your actual cleanup logic:
    // Example KV DB: db->clear();
    // Example SQL: TRUNCATE TABLE test; or DROP TABLE test;
    return true;
  }

 private:
  LSVPS* page_store;
  VDLS* value_store;
  DMMTrie* trie;
  uint64_t version;
};
