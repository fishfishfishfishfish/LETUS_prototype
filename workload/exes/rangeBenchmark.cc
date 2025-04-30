#include <chrono>
#include <fstream>

#include "DMMTrie.hpp"
#include "LSVPS.hpp"
#include "generator.hpp"

inline char RandomPrintChar(uint64_t num) {
  static const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  return charset[num % (sizeof(charset) - 1)];
}

std::string BuildKeyName(uint64_t key_num, int key_len) {
  std::string key_num_str = std::to_string(key_num);
  int zeros = key_len - key_num_str.length();
  zeros = std::max(0, zeros);
  std::string key_name = "";
  return key_name.append(zeros, '0').append(key_num_str);
}

int main(int argc, char** argv) {
  uint64_t num_accout = 5000;  // 40,000,000(40M) 2,000,000(2M)
  uint64_t load_batch_size = 100;
  uint64_t num_range_test = 10;
  uint64_t key_len = 32;
  uint64_t value_len = 1024;
  std::vector<uint64_t> ranges = {5, 50, 100, 200, 300, 400, 500, 1000, 2000};
  std::string data_path = "data/";
  std::string index_path = "index";
  std::string result_path = "exps/results/test.csv";

  int opt;
  while ((opt = getopt(argc, argv, "a:b:t:z:k:v:l:d:i:r:")) != -1) {
    switch (opt) {
      case 'a':  // num_accout
      {
        char* strtolPtr;
        num_accout = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (num_accout <= 0)) {
          std::cerr << "option -b requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'b':  // load_batch_size
      {
        char* strtolPtr;
        load_batch_size = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') ||
            (load_batch_size <= 0)) {
          std::cerr << "option -n requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 't':  // num_txn
      {
        char* strtolPtr;
        num_range_test = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') ||
            (num_range_test <= 0)) {
          std::cerr << "option -k requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'k':  // length of key.
      {
        char* strtolPtr;
        key_len = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (key_len <= 0)) {
          std::cerr << "option -k requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'v':  // length of value.
      {
        char* strtolPtr;
        value_len = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (value_len <= 0)) {
          std::cerr << "option -v requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'l': {  // range for range query
        std::string str = optarg;
        std::stringstream ss(str);
        std::string item;
        ranges.clear();
        while (std::getline(ss, item, ',')) {
          try {
            ranges.push_back(std::stoull(item));
          } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid number format for option -l: " << item
                      << std::endl;
            return 1;
          } catch (const std::out_of_range& e) {
            std::cerr << "Number out of range for option -l: " << item
                      << std::endl;
            return 1;
          }
        }
        break;
      }

      case 'd':  // data path
      {
        data_path = optarg;
        break;
      }

      case 'i':  // index path
      {
        index_path = optarg;
        break;
      }

      case 'r':  // result path
      {
        result_path = optarg;
        break;
      }

      default:
        std::cerr << "Unknown argument " << argv[optind] << std::endl;
        break;
    }
  }
  // init database
  // timeout = 100ms, the time between update the tree
  LSVPS* page_store = new LSVPS(index_path);
  VDLS* value_store = new VDLS(data_path);
  DMMTrie* trie = new DMMTrie(0, page_store, value_store);
  page_store->RegisterTrie(trie);

  // prepare result file
  std::ofstream rs_file;
  rs_file.open(result_path, std::ios::trunc);
  rs_file << "version,range,latency,throughput" << std::endl;
  rs_file.close();
  rs_file.open(result_path, std::ios::app);

  // load data
  key_len += key_len % 2 ? 0 : 1;  // make sure key_len is odd
  int num_load_version = num_accout / load_batch_size;
  int version = 0;
  CounterGenerator key_generator(1);
  for (; version <= num_load_version; version++) {
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < int(load_batch_size); i++) {
      uint64_t num = key_generator.Next();
      std::string key = BuildKeyName(num, key_len);
      std::string val = "";
      val = val.append(value_len, RandomPrintChar(num));
      trie->Put(0, version, key, val);
    }
    trie->Commit(version);
    auto end = std::chrono::system_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double load_latency = double(duration.count()) *
                          std::chrono::nanoseconds::period::num /
                          std::chrono::nanoseconds::period::den;
    if (version % 1 == 0) {
      std::cout << "version " << version << ", load latnecy:" << load_latency
                << ", load throughput:" << load_batch_size / load_latency
                << std::endl;
      rs_file << version << ",-1," << load_latency << ","
              << load_batch_size / load_latency << std::endl;
    }
  }
  sleep(1);

  uint64_t max_range = *std::max_element(ranges.begin(), ranges.end());
  uint64_t random_keys[num_range_test];
  UniformGenerator q_key_generator(1, (num_accout - max_range));
  for (int i = 0; i < int(num_range_test); i++) {
    random_keys[i] = q_key_generator.Next();
  }

  // range query
  version -= 1;
  for (uint64_t r : ranges) {
    for (int t = 0; t < int(num_range_test); t++) {
    int txn_key_id = 0;
      uint64_t num = random_keys[txn_key_id];
      auto start = std::chrono::system_clock::now();
      for (int ri = 0; ri < r; ri++) {
        std::string key = BuildKeyName(num + ri, key_len);
        trie->Get(0, version, key);
      }
      auto end = std::chrono::system_clock::now();
      auto duration =
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
      double range_latency = double(duration.count()) *
                             std::chrono::nanoseconds::period::num /
                             std::chrono::nanoseconds::period::den;
      std::cout << "version " << version << ", range" << r
                << ", range latnecy:" << range_latency
                << ", range throughput:" << r / range_latency << std::endl;
      rs_file << version << "," << r << "," << range_latency << ","
              << r / range_latency << std::endl;
      txn_key_id += 1;
    }
  }
  std::cout << "finished" << std::endl;
  rs_file.close();

  return 0;
}