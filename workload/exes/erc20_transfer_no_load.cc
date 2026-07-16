// ERC-20-style transfer benchmark aligned with docs/ercBenchmark_doc_zh.md.
//
// Key layout (39 bytes): "-account" (7) + contract (16) + holder (16).
//   - 16B address = big-endian uint64(idx) zero-padded to 16B (deriveAddress).
//   - contract index space  : [0,                     numContracts)
//   - holder   index space  : [numContracts,          numContracts+numHolders)
// Value = uint64 little-endian (8 bytes).
//
// Flags match the doc:
//   -a num_accounts         (default 100000000)
//   -n num_transactions     (default 10000,    --num-transactions)
//   -R contract_ratio       (default 0.1,      --contract-ratio)
//   -b load_batch_size      (default 20000)
//   -p tx_per_block         (default 1000,     --tx-per-block)
//   -B base_data            (default 0,        --base-data)
//   -m initial_balance_max  (default 1000000,  --initial-balance-max)
//   -x max_value            (default 100,      --max-value)
//   -M max_blocks           (default 0,        --max-blocks; 0=unlimited)
//   -s seed                 (default 42,       --seed)
//   -k key_len              (default 39, total composite key length)
//   -v value_len            (default 9, ignored; value is fixed 8B)
//   -d data_path            (default "data/")
//   -i index_path           (default "index")
//   -o result_path          (default "exps/results/ercBenchmark.csv")

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
#include <random>
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

// 8-byte little-endian uint64 encoding (matches doc value semantics).
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

void Mkdirs(const std::string& path) {
  if (path.empty()) return;
  std::string acc;
  for (char c : path) {
    acc.push_back(c);
    if (c == '/') mkdir(acc.c_str(), 0755);
  }
  mkdir(acc.c_str(), 0755);
}

// 16-byte address derived from a uint64 index: BE uint64 zero-padded.
std::string DeriveAddress(uint64_t key_num, int key_len) {
  std::string key_num_str = std::to_string(key_num);
  int zeros = key_len/2 - key_num_str.length();
  zeros = std::max(0, zeros);
  std::string key_name = "";
  if (key_num_str.length() >  key_len/2){
    cout << "DeriveAddress: key_num_str=" << key_num_str << ", key_num=" 
      << key_num << endl;
  }
  return key_name.append(zeros, '0').append(key_num_str);
}


// Composite 39-byte key: "-account" + 16B contract + 16B holder.
std::string MakeErcKey(const std::string& contract16,
                       const std::string& holder16,
                       int key_len) {
  std::string key = "";
  key.append(contract16);
  key.append(holder16);
  int zeros = key_len - key.size();
  zeros = std::max(0, zeros);
  key.append(zeros, '0');
  return key;
}


}  // namespace

int main(int argc, char** argv) {
  // Install handlers early so segfaults in setup are also captured.
  InstallSignalHandlers();

  try {
  int num_accout = 100000000;
  uint64_t num_txn = 36029300000ULL;
  double contract_ratio = 0.1;
  int load_batch_size = 20000;
  int tx_per_block = 1000;
  uint64_t base_data = 0;
  uint64_t initial_balance_max = 1000000ULL;
  uint64_t max_value = 100ULL;
  int max_blocks = 0;
  uint64_t seed = 42ULL;
  int key_len = 32;  // 16 + 16
  int value_len = 9;
  std::string data_path = "data/";
  std::string index_path = "index";
  std::string result_path = "exps/results/ercBenchmark.csv";

  int opt;
  while ((opt = getopt(argc, argv,
                      "a:n:R:b:p:B:m:x:M:s:k:v:d:i:o:")) != -1) {
    switch (opt) {
      case 'a':
      {
        char* p;
        num_accout = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (num_accout <= 0)) {
          std::cerr << "option -a requires a positive numeric arg\n";
        }
        break;
      }
      case 'n':
      {
        char* p;
        num_txn = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (num_txn <= 0)) {
          std::cerr << "option -n requires a positive numeric arg\n";
        }
        break;
      }
      case 'R':
      {
        char* p;
        contract_ratio = strtod(optarg, &p);
        if ((*optarg == '\0') || (*p != '\0') || (contract_ratio <= 0.0) ||
            (contract_ratio >= 1.0)) {
          std::cerr << "option -R requires a double in (0, 1)\n";
          contract_ratio = 0.1;
        }
        break;
      }
      case 'b':
      {
        char* p;
        load_batch_size = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (load_batch_size <= 0)) {
          std::cerr << "option -b requires a positive numeric arg\n";
        }
        break;
      }
      case 'p':
      {
        char* p;
        tx_per_block = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (tx_per_block <= 0)) {
          std::cerr << "option -p requires a positive numeric arg\n";
        }
        break;
      }
      case 'B':
      {
        char* p;
        base_data = strtoull(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0')) {
          std::cerr << "option -B requires a numeric arg\n";
        }
        break;
      }
      case 'm':
      {
        char* p;
        initial_balance_max = strtoull(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0')) {
          std::cerr << "option -m requires a numeric arg\n";
        }
        break;
      }
      case 'x':
      {
        char* p;
        max_value = strtoull(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (max_value == 0)) {
          std::cerr << "option -x requires a positive numeric arg\n";
        }
        break;
      }
      case 'M':
      {
        char* p;
        max_blocks = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0')) {
          std::cerr << "option -M requires a numeric arg\n";
        }
        break;
      }
      case 's':
      {
        char* p;
        seed = strtoull(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0')) {
          std::cerr << "option -s requires a numeric arg\n";
        }
        break;
      }
      case 'k':
      {
        char* p;
        key_len = strtoul(optarg, &p, 10);
        if ((*optarg == '\0') || (*p != '\0') || (key_len <= 0)) {
          std::cerr << "option -k requires a positive numeric arg\n";
        }
        break;
      }
      case 'v':
      {
        char* p;
        value_len = strtoul(optarg, &p, 10);
        break;
      }
      case 'd': data_path = optarg; break;
      case 'i': index_path = optarg; break;
      case 'o': result_path = optarg; break;
      default:
        std::cerr << "Unknown argument\n";
        break;
    }
  }

  // Derive doc-defined constants.
  int num_contracts = static_cast<int>(
      contract_ratio * static_cast<double>(num_accout));
  if (num_contracts <= 0) num_contracts = 1;
  uint64_t holders_total = (num_accout > num_contracts)
                               ? static_cast<uint64_t>(num_accout - num_contracts)
                               : 1ULL;
  uint64_t num_holders_pool = base_data / num_contracts;
  if (num_holders_pool == 0) num_holders_pool = 1;
  // Per the doc: holders are in [numContracts, numContracts + numHolders).
  uint64_t holder_base = static_cast<uint64_t>(num_contracts);

  // Init db.
  LSVPS* page_store = new LSVPS(index_path);
  VDLS* value_store = new VDLS(data_path);
  DMMTrie* trie = new DMMTrie(0, page_store, value_store);
  page_store->RegisterTrie(trie);

  // Seeded RNGs (offset to avoid overlap across streams).
  UniformGenerator initial_balance_gen(0, initial_balance_max, seed + 1);

  // ---- LOAD PHASE DISABLED ----
  // Start txn phase on an empty trie; version begins at 1 (no prior commits).
  int version = 1;
  std::cout << "load phase disabled; entering txn phase on empty trie at version "
            << version << std::endl;

  // ---- TXN RUN PHASE (block-streamed, skip-aware, CSV) ----
  uint64_t total_tx = 0;
  uint64_t executed_tx = 0;
  uint64_t skipped_not_found = 0;
  uint64_t skipped_low_balance = 0;
  uint64_t block_count = 0;
  auto txn_phase_start = chrono::system_clock::now();
  
  // Hot/cold holder selection: 80% prob -> front 20% of holders (hot),
  // 20% prob -> remaining 80% (cold). Drawn lazily to avoid buffering
  // num_txn*2 holder indices.
  UniformGenerator active_judger(0, 1000, seed + 2);
    // Hot pool sits within the ERC holder range so picks are guaranteed to
  // be valid addresses (avoid relying on --num-accounts scaling the
  // address index space).
  uint64_t contract_hot_ceiling =
      num_contracts * 0.2 > 0 ? num_contracts * 0.2 : 1;
  if (contract_hot_ceiling > (holder_base-1)) contract_hot_ceiling = (holder_base-1);
  UniformGenerator active_contract_key_generator(0,
                                               contract_hot_ceiling - 1,
                                               seed + 3);
  UniformGenerator inactive_contract_key_generator(contract_hot_ceiling,
                                                 holder_base - 1,
                                                 seed + 4);
  auto next_contract_idx = [&]() -> uint64_t {
    if (active_judger.Next() < 800) {
      return active_contract_key_generator.Next();
    }
    return inactive_contract_key_generator.Next();
  };
  // Hot pool sits within the ERC holder range so picks are guaranteed to
  // be valid addresses (avoid relying on --num-accounts scaling the
  // address index space).
  uint64_t holder_hot_ceiling =
      holder_base + (num_holders_pool * 0.2 > 0 ? num_holders_pool * 0.2 : 1);
  if (holder_hot_ceiling > num_accout) holder_hot_ceiling = num_accout;
  UniformGenerator active_holder_key_generator(holder_base,
                                               holder_hot_ceiling - 1,
                                               seed + 5);
  UniformGenerator inactive_holder_key_generator(holder_hot_ceiling,
                                                 num_accout - 1,
                                                 seed + 6);
  auto next_holder_idx = [&]() -> uint64_t {
    if (active_judger.Next() < 800) {
      return active_holder_key_generator.Next();
    }
    return inactive_holder_key_generator.Next();
  };

  UniformGenerator amount_gen(1, max_value, seed + 5);


  int tx_done = 0;
  double txn_elapsed = 0.0;
  while (tx_done < num_txn) {
    std::vector<std::pair<std::string, uint64_t>> block_puts;
    int entries_in_block = 0;
    while (entries_in_block < tx_per_block && tx_done < num_txn) {
      uint64_t ci = next_contract_idx();
      std::string contract16 = DeriveAddress(ci, key_len);

      uint64_t hi_from = next_holder_idx();
      uint64_t hi_to = next_holder_idx();
      // Tiny rejection-sampling loop to keep from != to.
      int retries = 0;
      while (hi_to == hi_from && retries < 16) {
        hi_to = next_holder_idx();
        ++retries;
      }
      if (hi_to == hi_from) hi_to = (hi_from + 1) % num_holders_pool;

      std::string from_addr = DeriveAddress(hi_from, key_len);
      std::string to_addr = DeriveAddress(hi_to, key_len);
      std::string key_from = MakeErcKey(contract16, from_addr, key_len);
      std::string key_to = MakeErcKey(contract16, to_addr, key_len);

      total_tx++;
      tx_done++;
      entries_in_block++;

      // 2 reads on the latest committed version (contract, from).
      DMMTrieProof proof_from = trie->GetProof(0, version - 1, key_from);
      DMMTrieProof proof_to = trie->GetProof(0, version - 1, key_to);

      // Skip rule 1+2: missing (contract, holder) pair.
      uint64_t bal_from = 0;
      uint64_t bal_to = 0;
      if (proof_from.value.empty()){
        bal_from = initial_balance_gen.Next();
      } else{
        bal_from = DecodeU64LE(proof_from.value);
      }
      if (proof_to.value.empty()){
        bal_to = initial_balance_gen.Next();
      } else{
         bal_to = DecodeU64LE(proof_to.value);
      }

      // Skip rule 3: from_balance < value.
      uint64_t value = amount_gen.Next();
      if (bal_from < value) {
        skipped_low_balance++;
        continue;
      }
      bal_from -= value;
      bal_to += value;
      executed_tx++;
      block_puts.emplace_back(key_from, bal_from);
      block_puts.emplace_back(key_to, bal_to);
    }
    block_count++;

    // Apply all puts in this block; commit once per block.
    auto tmp_phase_start = chrono::system_clock::now();
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

    if (max_blocks > 0 && static_cast<int>(block_count) >= max_blocks) {
      std::cout << "max_blocks reached, stop" << std::endl;
      break;
    }
    double tmp_throughput =
      (txn_elapsed > 0) ? static_cast<double>(executed_tx) / txn_elapsed : 0.0;

    if(block_count % 10 == 0){
      std::cout << "block " << block_count << " (version " << version
                << ") entries=" << entries_in_block
                << " executed=" << executed_tx
                << " skippedNotFound=" << skipped_not_found
                << " skippedLowBalance=" << skipped_low_balance 
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
      (txn_elapsed > 0) ? static_cast<double>(executed_tx) / txn_elapsed : 0.0;

  std::cout << "process " << num_txn << " transactions, current version is "
            << version << std::endl;

  // ---- CSV ----
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
