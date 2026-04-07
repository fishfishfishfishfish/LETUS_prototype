#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

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

//计算某目录所占空间大小（包含本身的4096Byte）
uint64_t GetDirectorySize(std::string dir) {
  DIR* dp;
  struct dirent* entry;
  struct stat statbuf;
  uint64_t totalSize = 0;

  if ((dp = opendir(dir.c_str())) == NULL) {
    fprintf(stderr, "Cannot open dir: %s\n", dir.c_str());
    return -1;  //可能是个文件，或者目录不存在
  }

  //先加上自身目录的大小
  lstat(dir.c_str(), &statbuf);
  totalSize += statbuf.st_size;

  while ((entry = readdir(dp)) != NULL) {
    char subdir[256];
    sprintf(subdir, "%s/%s", dir.c_str(), entry->d_name);
    lstat(subdir, &statbuf);

    if (S_ISDIR(statbuf.st_mode)) {
      if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) {
        continue;
      }

      long long int subDirSize = GetDirectorySize(subdir);
      totalSize += subDirSize;
    } else {
      totalSize += statbuf.st_size;
    }
  }

  closedir(dp);
  return totalSize;
}

int main(int argc, char** argv) {
  uint64_t num_accout = 5000;  // 40,000,000(40M) 2,000,000(2M)
  uint64_t update_count = 10;
  uint64_t key_len = 32;
  uint64_t value_len = 1024;
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

      case 't':  // num_txn
      {
        char* strtolPtr;
        update_count = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (update_count <= 0)) {
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
  rs_file << "version,latency,throughput,size" << std::endl;
  rs_file.close();
  rs_file.open(result_path, std::ios::app);

  key_len += key_len % 2 ? 0 : 1;  // make sure key_len is odd
  uint64_t version = 0;
  CounterGenerator key_generator(1);
  for (; version <= update_count; version++) {
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < int(num_accout); i++) {
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
    uint64_t data_size = GetDirectorySize(data_path);
    data_size += GetDirectorySize(index_path);
    if (version % 1 == 0) {
      std::cout << "version " << version << ", load latnecy:" << load_latency
                << ", load throughput:" << num_accout / load_latency
                << ", data dir is " << data_size << std::endl;
      rs_file << version << "," << load_latency << ","
              << num_accout / load_latency << "," << data_size << std::endl;
    }
  }

  std::cout << "finished" << std::endl;
  rs_file.close();
  return 0;
}