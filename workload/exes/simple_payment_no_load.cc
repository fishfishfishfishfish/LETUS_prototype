#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "DMMTrie.hpp"
#include "LSVPS.hpp"
#include "generator.hpp"

namespace {

// Print a bounded backtrace to stderr so we can localize crashes that
// bypass the C++ exception path (segfault, stack overflow, abort).
void PrintStackTrace(int max_frames = 32) {
  void* buf[32];
  int n = std::min(max_frames, 32);
  n = backtrace(buf, n);
  std::cerr << "backtrace(" << n << " frames):" << std::endl;
  backtrace_symbols_fd(buf, n, STDERR_FILENO);
}

// Signal handler: catch segfault/stack overflow/abort before the OS prints
// something cryptic. Just logs and re-raises the default handler so a
// core dump / clean exit still happens.
void OnFatalSignal(int sig) {
  std::cerr << "FATAL: received signal " << sig
            << " (likely segfault/stack overflow/abort)" << std::endl;
  PrintStackTrace();
  std::cerr.flush();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

void InstallSignalHandlers() {
  std::signal(SIGSEGV, OnFatalSignal);
  std::signal(SIGABRT, OnFatalSignal);
  std::signal(SIGFPE,  OnFatalSignal);
  std::signal(SIGBUS,  OnFatalSignal);
}

// Value encoding: uint64 little-endian, fixed 8 bytes (per txBenchmark_doc_zh.md).
std::string EncodeU64LE(uint64_t v) {
  std::string s(8, '\0');
  for (int i = 0; i < 8; i++) {
    s[i] = static_cast<char>((v >> (8 * i)) & 0xff);
  }
  return s;
}

uint64_t DecodeU64LE(const std::string& s) {
  uint64_t v = 0;
  for (int i = 0; i < 8 && i < static_cast<int>(s.size()); i++) {
    v |= (static_cast<uint64_t>(static_cast<unsigned char>(s[i])) << (8 * i));
  }
  return v;
}

// Recursively create a directory path. Best-effort; ignore EEXIST.
void Mkdirs(const std::string& path) {
  if (path.empty()) return;
  std::string acc;
  for (char c : path) {
    acc.push_back(c);
    if (c == '/') {
      mkdir(acc.c_str(), 0755);
    }
  }
  mkdir(acc.c_str(), 0755);
}

}  // namespace

std::string BuildKeyName(uint64_t key_num, int key_len) {
  std::string key_num_str = std::to_string(key_num);
  int zeros = key_len - key_num_str.length();
  zeros = std::max(0, zeros);
  std::string key_name = "";
  return key_name.append(zeros, '0').append(key_num_str);
}

int main(int argc, char** argv) {
  // Install handlers early so segfaults in setup are also captured.
  InstallSignalHandlers();

  try {
  int num_accout = 100000000;  // 40,000,000(40M) 2,000,000(2M)
  int load_batch_size = 20000;
  uint64_t num_txn = 36029300000ULL;
  int tx_per_block = 1000;       // Aligned with doc: --tx-per-block.
  int key_len = 9;
  int value_len = 9;
  uint64_t initial_balance_max = 1000000ULL;  // Aligned with doc: --initial-balance-max.
  uint64_t max_value = 100ULL;               // Aligned with doc: --max-value.
  uint64_t seed = 42ULL;                     // Aligned with doc: --seed.
  std::string data_path = "data/";
  std::string index_path = "index";
  std::string result_path = "exps/results/test.csv";

  int opt;
  while ((opt = getopt(argc, argv, "a:b:t:k:v:d:i:r:m:x:p:s:")) != -1) {
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
          std::cerr << "option -b requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 't':  // num_txn
      {
        char* strtolPtr;
        num_txn = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (num_txn <= 0)) {
          std::cerr << "option -t requires a numeric arg\n" << std::endl;
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

      case 'm':  // initial_balance_max
      {
        char* strtolPtr;
        initial_balance_max = strtoull(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0')) {
          std::cerr << "option -m requires a numeric arg\n" << std::endl;
        }
        break;
      }

      case 'x':  // max_value (transfer amount)
      {
        char* strtolPtr;
        max_value = strtoull(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (max_value == 0)) {
          std::cerr << "option -x requires a positive numeric arg\n"
                    << std::endl;
        }
        break;
      }

      case 'p':  // tx_per_block
      {
        char* strtolPtr;
        tx_per_block = strtoul(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0') || (tx_per_block <= 0)) {
          std::cerr << "option -p requires a positive numeric arg\n"
                    << std::endl;
        }
        break;
      }

      case 's':  // RNG seed
      {
        char* strtolPtr;
        seed = strtoull(optarg, &strtolPtr, 10);
        if ((*optarg == '\0') || (*strtolPtr != '\0')) {
          std::cerr << "option -s requires a numeric arg\n" << std::endl;
        }
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

  // ---- LOAD PHASE DISABLED ----
  // Start txn phase on an empty trie; version begins at 1 (no prior commits).
  int version = 1;
  // Per doc: initial balance is uniform in [0, initial_balance_max].
  // All RandomGenerators are seeded with --seed (doc: --seed) so the
  // workload is reproducible. Each one gets a distinct seed so the streams
  // do not overlap.
  UniformGenerator initial_balance_gen(0, initial_balance_max, seed + 1);
  std::cout << "load phase disabled; entering txn phase on empty trie at version "
            << version << std::endl;

  // transaction
  // Per txBenchmark_doc_zh.md:
  //   - generate tx-per-block entries at a time, immediately execute the
  //     block and write the CSV result.
  //   - Skip rules: from/to missing -> skippedNotFound; from < value ->
  //     skippedLowBalance.
  //   - Transfer value uniform in [1, max_value].
  //   - All puts in a block are committed together (one commit per block).

  // LETUS: 80% of accounts are called 20% of the time,
  //   while the remaining 20% of accounts are called 80% of the time.
  // Hot/cold key selection is done lazily inside the txn loop to avoid
  // materializing all num_txn*2 account ids in memory.
  UniformGenerator active_judger(0, 1000, seed + 2);  // if access active accounts
  //   80% prob -> front 20% (hot), 20% prob -> remaining 80% (cold).
  UniformGenerator active_key_generator(1, num_accout * 0.2, seed + 3);
  UniformGenerator inactive_key_generator(num_accout * 0.2, num_accout, seed + 4);
  auto next_account_idx = [&]() -> uint64_t {
    if (active_judger.Next() < 800) {
      return active_key_generator.Next();
    }
    return inactive_key_generator.Next();
  };
  // Per-doc transfer amount generator: uniform in [1, max_value].
  UniformGenerator amount_gen(1, max_value, seed + 5);

  uint64_t total_tx = 0;
  uint64_t executed_tx = 0;
  uint64_t skipped_not_found = 0;
  uint64_t skipped_low_balance = 0;
  uint64_t block_count = 0;
  auto txn_phase_start = chrono::system_clock::now();

  int tx_done = 0;
  double txn_elapsed = 0.0;
  while (tx_done < num_txn) {
    int entries_in_block = 0;
    std::vector<std::pair<std::string, uint64_t>> block_puts;
    while (entries_in_block < tx_per_block && tx_done < num_txn) {
      std::string key_send = BuildKeyName(next_account_idx(), key_len);
      std::string key_recv = BuildKeyName(next_account_idx(), key_len);
      total_tx++;
      tx_done++;
      entries_in_block++;

      // Per-doc: 2 reads (from, to) using the latest committed version.
      DMMTrieProof proof_send = trie->GetProof(0, version - 1, key_send);
      DMMTrieProof proof_recv = trie->GetProof(0, version - 1, key_recv);

      // Skip rule: missing account.
      uint64_t value_send = 0;
      uint64_t value_recv = 0;
      if (proof_send.value.empty()){
        value_send = initial_balance_gen.Next();
      } else{
        value_send = DecodeU64LE(proof_send.value);
      }
      if (proof_recv.value.empty()){
        value_recv = initial_balance_gen.Next();
      } else{
        value_recv = DecodeU64LE(proof_recv.value);
      }

      // Skip rule: from_balance < value.
      uint64_t value = amount_gen.Next();
      if (value_send < value) {
        skipped_low_balance++;
        continue;
      }
      value_send -= value;
      value_recv += value;
      executed_tx++;
      block_puts.emplace_back(key_send, value_send);
      block_puts.emplace_back(key_recv, value_recv);
    }
    block_count++;

    auto tmp_phase_start = chrono::system_clock::now();
    // Apply all puts in this block; commit once per block.
    for (const auto& kv : block_puts) {
      trie->Put(0, version, kv.first, EncodeU64LE(kv.second));
    }
    trie->Commit(version);
    auto tmp_phase_end = chrono::system_clock::now();
    double tmp_elapsed =
      chrono::duration_cast<chrono::microseconds>(tmp_phase_end -
                                                  tmp_phase_start)
          .count() * 1.0 * 
      chrono::microseconds::period::num /
      chrono::microseconds::period::den;
    txn_elapsed += tmp_elapsed;
    // txn_elapsed =
    //   chrono::duration_cast<chrono::microseconds>(tmp_phase_end -
    //                                               txn_phase_start)
    //       .count() *
    //   chrono::microseconds::period::num /
    //   chrono::microseconds::period::den;
    double tmp_throughput =
      (txn_elapsed > 0) ? static_cast<double>(executed_tx) / txn_elapsed : 0.0;

    if(block_count % 2000 == 0 || (block_count >= 9790 && block_count <= 9810)){
      std::cout << "block " << block_count << " (version " << version
                << ") entries=" << entries_in_block
                << " executed=" << executed_tx
                << " skippedNotFound=" << skipped_not_found
                << " skippedLowBalance=" << skipped_low_balance 
                << " txn_elapsed=" << txn_elapsed
                << " tmp_elapsed=" << tmp_elapsed
                << " tmp_throughput=" << tmp_throughput
                << std::endl;
    }
    version++;
  }
  auto txn_phase_end = chrono::system_clock::now();
  double total_txn_elapsed =
      chrono::duration_cast<chrono::microseconds>(txn_phase_end -
                                                  txn_phase_start)
          .count() *
      chrono::microseconds::period::num /
      chrono::microseconds::period::den;
  double throughput =
      (txn_elapsed > 0) ? static_cast<double>(num_txn) / txn_elapsed : 0.0;

  std::cout << "process " << num_txn << " transactions, current version is "
            << version << std::endl;

  // CSV: TotalTx, ExecutedTx, SkippedNotFound, SkippedLowBalance,
  //      BlockCount, Elapsed(s), Throughput(tx/sec).
  {
    size_t slash = result_path.find_last_of('/');
    if (slash != std::string::npos) {
      Mkdirs(result_path.substr(0, slash));
    }
    std::ofstream out(result_path);
    out << "TotalTx,ExecutedTx,SkippedNotFound,SkippedLowBalance,BlockCount,"
           "TxnElapsed(s),TotalElapsed(s),Throughput\n";
    out << total_tx << "," << executed_tx << "," << skipped_not_found << ","
        << skipped_low_balance << "," << block_count << "," << txn_elapsed
        << "," << total_txn_elapsed << "," << " " << throughput << "\n";
    std::cout << "result written to " << result_path << std::endl;
  }

  return true;
  } catch (const std::exception& e) {
    std::cerr << "FATAL: " << e.what() << std::endl;
    PrintStackTrace();
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "FATAL: non-std exception" << std::endl;
    PrintStackTrace();
    return EXIT_FAILURE;
  }
}