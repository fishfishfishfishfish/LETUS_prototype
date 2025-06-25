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
  uint64_t num_txn_version = 0;
  uint64_t num_txn_account = 50;
  uint64_t key_len = 32;
  uint64_t value_len = 1024;
  std::vector<size_t> oldest_query_versions = {1, 2, 4, 10, 20, 40};
  std::vector<size_t> query_version_count = {1, 1, 1, 1, 1, 1};
  std::string data_path = "data/";
  std::string index_path = "index";
  std::string result_path = "exps/results/test.csv";

  int opt;
  while ((opt = getopt(argc, argv, "a:b:t:z:p:k:v:l:q:d:i:r:")) != -1) {
    switch (opt) {
      case 'a':  // num_accout
      {
        char* strtolPtr;
        num_accout = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (num_accout <= 0)) {
          std::cerr << "option -a requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'b':  // load_batch_size
      {
        char* strtolPtr;
        load_batch_size = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') ||
            (load_batch_size <= 0)) {
          std::cerr << "option -b requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 't':  // num_txn
      {
        char* strtolPtr;
        num_txn_version = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') ||
            (num_txn_version < 0)) {
          std::cerr << "option -t requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'z':  // num_txn_account
      {
        char* strtolPtr;
        num_txn_account = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') ||
            (num_txn_account <= 0)) {
          std::cerr << "option -z requires a numeric arg\n" << std::endl;
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

      case 'l': {  // latest version to query
        std::string str = optarg;
        std::stringstream ss(str);
        std::string item;
        oldest_query_versions.clear();
        query_version_count.clear();
        while (std::getline(ss, item, ',')) {
          try {
            // 用“|”切分item为两个数值
            std::stringstream ss2(item);
            std::string item2;
            std::vector<size_t> versions;
            while (std::getline(ss2, item2, '|')) {
              versions.push_back(std::stoul(item2));
            }
            oldest_query_versions.push_back(versions[0]);
            query_version_count.push_back(versions[1]);
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
  LSVPS* page_store = new LSVPS(index_path);
  VDLS* value_store = new VDLS(data_path);
  DMMTrie* trie = new DMMTrie(0, page_store, value_store);
  page_store->RegisterTrie(trie);

  // prepare result file
  std::ofstream rs_file;
  rs_file.open(result_path, std::ios::trunc);
  rs_file << "version,operation,latency,throughput" << std::endl;
  rs_file.close();
  rs_file.open(result_path, std::ios::app);

  key_len += key_len % 2 ? 0 : 1;  // make sure key_len is odd
  uint64_t num_load_version = num_accout / load_batch_size;
  uint64_t version = 0;

  // updates
  uint64_t random_keys[num_txn_account];
  UniformGenerator txn_key_generator(1, num_accout);
  for (int i = 0; i < num_txn_account; i++) {
    random_keys[i] = txn_key_generator.Next();
  }

  for (; version <= num_load_version + num_txn_version; version++) {
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < int(num_txn_account); i++) {
      uint64_t num = random_keys[i];
      std::string key = BuildKeyName(num, key_len);
      std::string val = "";
      val = val.append(value_len, RandomPrintChar(num));
      trie->Put(0, version, key, val);
    }
    trie->Commit(version);
    auto end = std::chrono::system_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double put_latency = double(duration.count()) *
                         std::chrono::nanoseconds::period::num /
                         std::chrono::nanoseconds::period::den;
    std::cout << "version " << version << ", put latnecy:" << put_latency << ","
              << "put throughput:" << num_txn_account / put_latency
              << std::endl;
    rs_file << version << ",PUT," << put_latency << ","
            << num_txn_account / put_latency << std::endl;
  }

  // load data
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
      rs_file << version << ",LOAD," << load_latency << ","
              << load_batch_size / load_latency << std::endl;
    }
  }

  // version get
  uint64_t cur_version = version - 1;
  for (int v_idx = 0; v_idx < oldest_query_versions.size(); v_idx++) {
    size_t nv = query_version_count[v_idx];
    size_t ov = cur_version - oldest_query_versions[v_idx];
    size_t lv = ov + nv;
    if (nv == -1 || lv > cur_version) {
      lv = cur_version;
      nv = cur_version - ov;
    }
    for (int t = 0; t < int(num_txn_account); t++) {
      std::string key = BuildKeyName(random_keys[t], key_len);
      std::map<uint64_t, std::vector<std::string>> keys;
      auto start = std::chrono::system_clock::now();
      for (size_t v = ov; v < lv; ++v) {
        trie->GetProof(0, v, key);
      }
      auto end = std::chrono::system_clock::now();
      auto duration =
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
      double get_latency = double(duration.count()) *
                           std::chrono::nanoseconds::period::num /
                           std::chrono::nanoseconds::period::den;
      std::cout << "nv: " << nv << "(" << ov << "-" << lv << ")"
                << ", get latnecy : " << get_latency
                << ", get throughput:" << nv / get_latency << std::endl;
      rs_file << oldest_query_versions[v_idx] << "|" << nv << ",GET,"
              << get_latency << "," << nv / get_latency << std::endl;
    }
  }
  std::cout << "finished" << std::endl;
  rs_file.close();

  return 0;
}