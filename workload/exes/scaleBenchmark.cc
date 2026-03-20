#include <dirent.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "mydatabase.hpp"

// Linux-only recursive directory creation
bool create_directory_recursive(const std::string& path) {
  std::string temp_path = path;
  std::replace(temp_path.begin(), temp_path.end(), '\\', '/');
  mode_t mode = 0755;
  size_t pos = 0;

  do {
    pos = temp_path.find_first_of('/', pos + 1);
    std::string sub_path = temp_path.substr(0, pos);
    if (sub_path.empty()) continue;

    // Create directory if not exists
    if (mkdir(sub_path.c_str(), mode) == -1) {
      if (errno != EEXIST) {
        std::cerr << "Failed to create directory: " << sub_path
                  << " (errno: " << errno << ")" << std::endl;
        return false;
      }
    }
  } while (pos != std::string::npos);

  return true;
}

// Linux-only directory size calculation (recursive)
uint64_t get_directory_total_size(const std::string& dir_path) {
  uint64_t total_size = 0;
  DIR* dir = opendir(dir_path.c_str());

  if (!dir) {
    std::cerr << "Failed to open directory: " << dir_path
              << " (errno: " << errno << ")" << std::endl;
    return 0;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string full_path = dir_path + "/" + entry->d_name;
    struct stat stat_buf;

    // Get file/directory stats
    if (stat(full_path.c_str(), &stat_buf) == -1) {
      std::cerr << "Failed to stat " << full_path << " (errno: " << errno << ")"
                << std::endl;
      continue;
    }

    // Recursively calculate subdirectory size
    if (S_ISDIR(stat_buf.st_mode)) {
      total_size += get_directory_total_size(full_path);
    }
    // Add regular file size
    else if (S_ISREG(stat_buf.st_mode)) {
      total_size += static_cast<uint64_t>(stat_buf.st_size);
    }
  }

  closedir(dir);
  return total_size;
}

uint64_t get_process_memory_usage() {
  FILE* file = fopen("/proc/self/statm", "r");
  if (!file) {
    return 0;
  }

  uint64_t size, resident, share, text, lib, data, dt;
  if (fscanf(file, "%lu %lu %lu %lu %lu %lu %lu", &size, &resident, &share,
             &text, &lib, &data, &dt) != 7) {
    fclose(file);
    return 0;
  }
  fclose(file);

  // resident is the resident set size in pages
  // page size is typically 4096 bytes on Linux
  long page_size = sysconf(_SC_PAGESIZE);
  return resident * page_size;
}

// Get peak memory usage using getrusage
uint64_t get_peak_memory_usage() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    // ru_maxrss is in kilobytes
    return usage.ru_maxrss * 1024;
  }
  return 0;
}

// Simple command-line argument parser
std::map<std::string, std::string> parse_arguments(int argc, char* argv[]) {
  std::map<std::string, std::string> args;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg.substr(0, 2) == "--") {
      size_t equal_pos = arg.find('=');
      if (equal_pos != std::string::npos) {
        std::string key = arg.substr(2, equal_pos - 2);
        std::string value = arg.substr(equal_pos + 1);
        args[key] = value;
      } else if (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string key = arg.substr(2);
        args[key] = argv[++i];
      } else {
        std::string key = arg.substr(2);
        args[key] = "true";
      }
    }
  }

  return args;
}

// Print usage instructions
void print_usage(const char* program_name) {
  std::cerr << "Usage: " << program_name << " [OPTIONS]" << std::endl;
  std::cerr << "Example: " << program_name
            << " --write-batch=10000 --read-batch=10000 --key-length=16 "
               "--value-length=256"
            << std::endl;
  std::cerr << "Example (with custom paths): " << program_name
            << " --db-dir=/tmp/my_test_db --result-file=my_results.csv"
            << std::endl;
  std::cerr << "Example (default values): " << program_name << std::endl;
  std::cerr << "Options:" << std::endl;
  std::cerr << "  --write-batch=NUM    Number of records for write throughput "
               "test (default: 10000)"
            << std::endl;
  std::cerr << "  --read-batch=NUM     Number of records for read throughput "
               "test (default: 10000)"
            << std::endl;
  std::cerr
      << "  --key-length=NUM     Length of database key in bytes (default: 16)"
      << std::endl;
  std::cerr << "  --value-length=NUM   Length of database value in bytes "
               "(default: 256)"
            << std::endl;
  std::cerr << "  --db-dir=PATH        Database storage directory (default: "
               "./test_database)"
            << std::endl;
  std::cerr << "  --result-dir=PATH    Result directory path (default: "
               "exps/results_ledgerdb)"
            << std::endl;
  std::cerr << "  --help               Show this help message" << std::endl;
}

// Validate command-line parameters
bool validate_parameters(int write_batch, int read_batch, int key_len,
                         int value_len) {
  if (write_batch <= 0 || read_batch <= 0 || key_len <= 0 || value_len <= 0) {
    std::cerr << "Error: All parameters must be positive integers!"
              << std::endl;
    return false;
  }
  if (key_len > 2048) {
    std::cerr << "Warning: Key length > 2048 may exceed database limits "
                 "(proceed with caution)"
              << std::endl;
  }
  if (value_len > 1048576) {
    std::cerr << "Warning: Value length > 1MB may cause performance issues "
                 "(proceed with caution)"
              << std::endl;
  }
  return true;
}

// Convert bytes to MB (2 decimal places)
double bytes_to_mb(uint64_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// Format number with comma as thousands separator
std::string format_with_commas(uint64_t number) {
  std::stringstream ss;
  ss.imbue(std::locale(""));  // Use system locale for formatting
  ss << number;
  return ss.str();
}

int main(int argc, char* argv[]) {
  // ====================== Command-Line Parameter Parsing
  // ====================== Parse command-line arguments
  auto args = parse_arguments(argc, argv);

  // Check for help flag
  if (args.count("help")) {
    print_usage(argv[0]);
    return 0;
  }

  // Default parameter values
  int TEST_WRITE_BATCH = 1000;
  int TEST_READ_BATCH = 1000;
  int KEY_LENGTH = 32;
  int VALUE_LENGTH = 256;
  std::string DB_DIR = "data/";
  std::string RESULT_DIR = "exps/results_ledgerdb/";
  std::string RESULT_CSV = RESULT_DIR + "scaleBenchmark.csv";

  // Parse named parameters
  if (args.count("write-batch")) {
    TEST_WRITE_BATCH = std::stoi(args["write-batch"]);
  }
  if (args.count("read-batch")) {
    TEST_READ_BATCH = std::stoi(args["read-batch"]);
  }
  if (args.count("key-length")) {
    KEY_LENGTH = std::stoi(args["key-length"]);
  }
  if (args.count("value-length")) {
    VALUE_LENGTH = std::stoi(args["value-length"]);
  }
  if (args.count("db-dir")) {
    DB_DIR = args["db-dir"];
  }
  if (args.count("result-dir")) {
    RESULT_DIR = args["result-dir"];
    RESULT_CSV = RESULT_DIR + "scaleBenchmark.csv";
  }
  if (args.count("result-file")) {
    RESULT_CSV = RESULT_DIR + args["result-file"];
  }

  // Display configuration
  std::cout << "Benchmark Configuration:" << std::endl;
  std::cout << "  Write Batch Size: " << TEST_WRITE_BATCH << " records"
            << std::endl;
  std::cout << "  Read Batch Size:  " << TEST_READ_BATCH << " records"
            << std::endl;
  std::cout << "  Key Length:       " << KEY_LENGTH << " bytes" << std::endl;
  std::cout << "  Value Length:     " << VALUE_LENGTH << " bytes" << std::endl;
  std::cout << "  Database Directory: " << DB_DIR << std::endl;
  std::cout << "  Result Directory:  " << RESULT_DIR << std::endl;
  std::cout << "  Result File:       " << RESULT_CSV << std::endl;
  std::cout << std::endl;

  // Validate parameters
  if (!validate_parameters(TEST_WRITE_BATCH, TEST_READ_BATCH, KEY_LENGTH,
                           VALUE_LENGTH)) {
    print_usage(argv[0]);
    return -1;
  }

  // ====================== Test Configuration ======================
  // Database storage directory and result file are now configurable via command
  // line Test scales: 1000 → 10k → 100k → 1M → 10M → 100M (1B requires
  // distributed environment)
  const std::vector<uint64_t> BASE_DATA_SIZES = {
      1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
  // const std::vector<uint64_t> BASE_DATA_SIZES = {
  //     1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

  // ====================== Initialization ======================
  // Create database directory (Linux-only)
  if (!create_directory_recursive(DB_DIR)) {
    std::cerr << "Failed to create database directory: " << DB_DIR << std::endl;
    return -1;
  }

  MyDatabase db(DB_DIR);

  // Open result CSV file and write header
  if (!create_directory_recursive(RESULT_DIR)) {
    std::cerr << "Failed to create result directory: " << RESULT_DIR
              << std::endl;
    return -1;
  }
  std::ofstream result_file(RESULT_CSV);
  if (!result_file.is_open()) {
    std::cerr << "Failed to create result file: " << RESULT_CSV << std::endl;
    return -1;
  }
  // CSV header with parameter info (traceability)
  result_file
      << "Base Data Scale,Write Batch Size,Read Batch Size,Key Length,Value "
         "Length,Write Throughput (records/sec),Read Throughput "
         "(records/sec),Total Disk Usage (MB),Average Per Record (bytes),"
         "Memory (MB),Peak Memory (MB),Memory Per Record (bytes)"
      << std::endl;

  // Print test configuration (for verification)
  std::cout << "====================== Test Configuration ====================="
            << std::endl;
  std::cout << "Write Batch Size: " << TEST_WRITE_BATCH << " records"
            << std::endl;
  std::cout << "Read Batch Size:  " << TEST_READ_BATCH << " records"
            << std::endl;
  std::cout << "Key Length:       " << KEY_LENGTH << " bytes" << std::endl;
  std::cout << "Value Length:     " << VALUE_LENGTH << " bytes" << std::endl;
  std::cout << "Test Scales:      ";
  for (size_t i = 0; i < BASE_DATA_SIZES.size(); ++i) {
    std::cout << BASE_DATA_SIZES[i];
    if (i < BASE_DATA_SIZES.size() - 1) std::cout << " → ";
  }
  std::cout << std::endl;
  std::cout << "==============================================================="
            << std::endl;

  // ====================== Efficient Sequential write for All Scales
  // ======================
  std::cout << "\n========== Starting Scale Benchmark ==========" << std::endl;

  // Step 1: Clear database and record baseline disk usage
  if (!db.clear_all_data()) {
    std::cerr << "Failed to clear database, aborting benchmark" << std::endl;
    return -1;
  }
  uint64_t empty_db_size = get_directory_total_size(DB_DIR);
  // Step 2: Calculate incremental data sizes for sequential testing
  std::vector<uint64_t> incremental_sizes;
  uint64_t current_size = 0;
  for (uint64_t target_size : BASE_DATA_SIZES) {
    if (target_size > current_size) {
      incremental_sizes.push_back(target_size - current_size);
      current_size = target_size;
    }
  }

  // Step 3: Sequentially write data and test at each target scale
  current_size = 0;
  uint64_t next_key = 1;  // Start from key 1

  for (size_t i = 0; i < BASE_DATA_SIZES.size(); i++) {
    uint64_t target_size = BASE_DATA_SIZES[i];
    uint64_t incremental_size = incremental_sizes[i];

    std::cout << "\n========== Testing at data scale: "
              << format_with_commas(target_size) << " ==========" << std::endl;

    // Write incremental base data starting from next_key
    if (incremental_size > 0) {
      std::cout << "Writing " << incremental_size
                << " incremental records (keys: " << next_key << "-"
                << next_key + incremental_size - 1 << ", total: " << target_size
                << ")" << std::endl;
      if (!db.write_base_data(incremental_size, KEY_LENGTH, VALUE_LENGTH,
                              next_key)) {
        std::cerr << "Failed to write incremental data, skipping scale "
                  << target_size << std::endl;
        continue;
      }

      std::cout << "Flush to disk..." << std::endl;
      db.flush();  // Force flush to disk
      std::cout << "Flush to disk done." << std::endl;

      // Update next_key for the next iteration
      next_key += incremental_size;
    }

    current_size = target_size;
    std::cout << "Test Results: " << std::endl;
    // Step 4: Test write throughput at current data scale
    double write_throughput = 0.0;
    if (!db.test_batch_write(current_size, TEST_WRITE_BATCH, KEY_LENGTH,
                             VALUE_LENGTH, write_throughput)) {
      std::cerr << "Write throughput test failed" << std::endl;
      write_throughput = -1;
    }
    std::cout << "  Write Throughput: " << std::fixed << std::setprecision(2)
              << write_throughput << " records/sec" << std::endl;

    // Step 5: Test read throughput at current data scale
    double read_throughput = 0.0;
    if (!db.test_batch_read(current_size, TEST_READ_BATCH, KEY_LENGTH,
                            read_throughput)) {
      std::cerr << "Read throughput test failed" << std::endl;
      read_throughput = -1;
    }
    std::cout << "  Read Throughput:  " << std::fixed << std::setprecision(2)
              << read_throughput << " records/sec" << std::endl;

    // Step 6: Calculate disk usage at current data scale
    uint64_t total_db_size = get_directory_total_size(DB_DIR);
    double total_db_mb = bytes_to_mb(total_db_size);
    // Average per record = (Total usage - Empty DB usage) / Current data scale
    double avg_per_record = static_cast<double>(total_db_size - empty_db_size) /
                            static_cast<double>(current_size);

    // Step 7: Calculate memory usage at current data scale
    uint64_t current_memory = get_process_memory_usage();
    uint64_t peak_memory = get_peak_memory_usage();
    double current_memory_mb = bytes_to_mb(current_memory);
    double peak_memory_mb = bytes_to_mb(peak_memory);
    // Memory per record = Current memory usage / Current data scale
    double memory_per_record =
        static_cast<double>(current_memory) / static_cast<double>(current_size);

    // Step 8: Output results (console + CSV)
    std::cout << "  Total Disk Usage: " << std::fixed << std::setprecision(2)
              << total_db_mb << " MB" << std::endl;
    std::cout << "  Average Per Record: " << std::fixed << std::setprecision(2)
              << avg_per_record << " bytes" << std::endl;
    std::cout << "  Current Memory:   " << std::fixed << std::setprecision(2)
              << current_memory_mb << " MB" << std::endl;
    std::cout << "  Peak Memory:      " << std::fixed << std::setprecision(2)
              << peak_memory_mb << " MB" << std::endl;
    std::cout << "  Memory Per Record: " << std::fixed << std::setprecision(2)
              << memory_per_record << " bytes" << std::endl;

    // Write to CSV (full parameter traceability)
    result_file << target_size << "," << TEST_WRITE_BATCH << ","
                << TEST_READ_BATCH << "," << KEY_LENGTH << "," << VALUE_LENGTH
                << "," << std::fixed << std::setprecision(2) << write_throughput
                << "," << std::fixed << std::setprecision(2) << read_throughput
                << "," << std::fixed << std::setprecision(2) << total_db_mb
                << "," << std::fixed << std::setprecision(2) << avg_per_record
                << "," << std::fixed << std::setprecision(2)
                << current_memory_mb << "," << std::fixed
                << std::setprecision(2) << peak_memory_mb << "," << std::fixed
                << std::setprecision(2) << memory_per_record << std::endl;
  }

  // ====================== Cleanup ======================
  result_file.close();
  db.clear_all_data();  // Final cleanup of all test data
  std::cout << "\nAll scale tests completed! Results saved to: " << RESULT_CSV
            << std::endl;

  return 0;
}