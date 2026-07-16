#!/bin/bash
# Aligned with workload/exes/erc20_transfer.cc and docs/ercBenchmark_doc_zh.md.
#
# Usage:
#   ./test_erc20.sh <db_name> <test_name>
#
# CLI flags of erc20_transfer (see erc20_transfer.cc):
#   -a num_accounts          (--num-accounts, default 100000000)
#   -n num_transactions      (--num-transactions, default 10000)
#   -R contract_ratio        (--contract-ratio, default 0.1)
#   -b load_batch_size       (default 20000)
#   -p tx_per_block          (--tx-per-block, default 1000)
#   -B base_data             (--base-data; computed at run time from
#                              BASE_DATA_RATIO, default 0)
#   -m initial_balance_max   (--initial-balance-max, default 1000000)
#   -x max_value             (--max-value, default 100)
#   -M max_blocks            (--max-blocks, default 0 = unlimited)
#   -s seed                  (--seed, default 42)
#   -d data_path
#   -i index_path
#   -o result_path           (CSV: TotalTx,ExecutedTx,SkippedNotFound,
#                             SkippedLowBalance,BlockCount,Elapsed(s),
#                             Throughput)
export ASAN_OPTIONS=detect_leaks=0

# 编译项目
cd ../
./build.sh

db_name=${1:-letus}
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
test_name=${2:-$TIMESTAMP}
echo "db_name: $db_name, test_name=$test_name"

# ---------------------------------------------------------------------------
# Helper: convert scientific notation to integer
#   5e7  -> 50000000
#   1e8  -> 100000000
#   1e9  -> 1000000000
# Plain integers are passed through unchanged.
# ---------------------------------------------------------------------------
parse_int() {
    local s="$1"
    # Use python if available (most portable for scientific notation).
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "import sys; print(int(float(sys.argv[1])))" "$s"
        return
    fi
    if command -v bc >/dev/null 2>&1; then
        printf "%.0f\n" "$(echo "$s" | bc -l)"
        return
    fi
    # Fallback: awk
    awk -v s="$s" 'BEGIN { printf("%.0f\n", s + 0) }'
}

# 也可解开注释以同时跑多档 account 规模（与 doc ercScaling 思路一致）。
# num_account=(10000000 100000000 1000000000)
# num_account=(10000000)
# scales=("5e4" "1e7" "5e7" "1e8" "5e8" "1e9")
scales=("5e8" "1e7" "5e7" "1e8" "1e9")

# load 阶段
load_batch_size=5000
# 2026-07-13: Addresses (Total) ：422,161,692, Contracts Deployed (Total) 102,222,053
contract_ratio=0.2421  # 24.21% of accounts are contracts
# Ratio used to derive BASE_DATA from --num-accounts at run time:
#   BASE_DATA = floor(num-accounts * BASE_DATA_RATIO)
# Set BASE_DATA_RATIO=0 to keep the doc's "degenerate to --num-accounts" path
# (i.e. hand the program an explicit 0 via -B).
BASE_DATA_RATIO=5.0
key_size=32

# txn 阶段（与 doc default 对齐）
num_txn=1000000
tx_per_block=4000
max_blocks=0

# 数值参数（与 doc default 对齐，可通过命令行覆盖）
initial_balance_max=1000000
max_value=100
seed=42

BIN_DIR="../build_release_letus/bin"
EXE="${BIN_DIR}/erc20_transfer"

cd exps/
data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/erc20_${test_name}"
log_dir="$PWD/logs/test_erc20_${test_name}"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"
echo "log_file: $log_file"
echo "executable: $EXE"

mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}
mkdir -p ${log_dir}
rm -rf ${result_dir}/*

# 运行测试
for scale in "${scales[@]}"; do
    n_acc=$(parse_int "$scale")
    # set -x
    # 清理数据文件夹
    rm -rf $data_path
    mkdir -p $data_path
    rm -rf $index_path
    mkdir -p $index_path

    result_path="${result_dir}/acc_${scale}.csv"
    log_file="$log_dir/test_erc20_${scale}.log"
    # Derive BASE_DATA from ratio at run time so that the same script works
    # for any num_account. floor() is computed via awk to keep the result as
    # an integer. When BASE_DATA_RATIO=0 we pass -B 0 to keep the doc's
    # "degenerate to --num-accounts" semantics.
    BASE_DATA=$(awk -v n="${n_acc}" -v r="${BASE_DATA_RATIO}" \
        'BEGIN { v = n * r; if (v < 0) v = 0; printf("%.0f", v); }')

    echo "args: n_acc=${n_acc}, contract_ratio=${contract_ratio}, \
BASE_DATA_RATIO=${BASE_DATA_RATIO} (-> BASE_DATA=${BASE_DATA}), \
load_batch_size=${load_batch_size}, num_txn=${num_txn}, \
tx_per_block=${tx_per_block}, max_blocks=${max_blocks}, \
key_size=${key_size}, initial_balance_max=${initial_balance_max}, max_value=${max_value}, \
seed=${seed}" | tee -a "$log_file"

    # 运行测试并写入 CSV（erc20_transfer 自身会 mkdir -p 父目录）。
    ${EXE} \
        -a ${n_acc} \
        -n ${num_txn} \
        -R ${contract_ratio} \
        -B ${BASE_DATA} \
        -b ${load_batch_size} \
        -p ${tx_per_block} \
        -M ${max_blocks} \
        -m ${initial_balance_max} \
        -x ${max_value} \
        -s ${seed} \
        -k ${key_size} \
        -d ${data_path} \
        -i ${index_path} \
        -o ${result_path} | tee -a "$log_file"

    sleep 5
    set +x
done
