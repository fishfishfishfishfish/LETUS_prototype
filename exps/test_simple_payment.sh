#!/bin/bash
# Aligned with workload/exes/simple_payment.cc and docs/txBenchmark_doc_zh.md.
#
# Usage:
#   ./test_simple_payment.sh <db_name> <test_name>
#
# CLI flags of simple_payment (see simple_payment.cc):
#   -a  num_account
#   -b  load_batch_size
#   -t  num_txn
#   -k  key_len
#   -m  initial_balance_max     (--initial-balance-max, default 1000000)
#   -x  max_value               (--max-value, default 100)
#   -p  tx_per_block            (--tx-per-block, default 1000)
#   -s  seed                    (--seed, default 42)
#   -d  data_path
#   -i  index_path
#   -r  result_path             (CSV: TotalTx,ExecutedTx,SkippedNotFound,
#                                SkippedLowBalance,BlockCount,Elapsed(s),
#                                Throughput)
export ASAN_OPTIONS=detect_leaks=0

# 编译项目
cd ../
./build.sh

# 定义测试参数
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

scales=("5e7" "1e8" "5e8" "1e9")
# scales=("1e8" "5e8" "1e9")

# load 阶段
load_batch_size=100000
key_size=32

# txn 阶段（与 doc default 对齐）
num_txn=1000000
tx_per_block=4000        # --tx-per-block: 每个 block 的转账笔数

# 数值参数（与 doc default 对齐，可通过命令行覆盖）
initial_balance_max=1000000
max_value=100
seed=42

BIN_DIR="../build_release_letus/bin"
EXE="${BIN_DIR}/simple_payment"

cd exps/
data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/simple-payment_${test_name}"
log_dir="$PWD/logs/test_simple_payment_${test_name}"
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
    log_file="$log_dir/test_simple_payment_acc_${scale}.log"
    echo "$(date "+%Y-%m-%d %H:%M:%S")"
    echo "args: n_acc=${n_acc}, load_batch_size=${load_batch_size}, \
num_txn=${num_txn}, tx_per_block=${tx_per_block}, \
key_size=${key_size}, \
initial_balance_max=${initial_balance_max}, max_value=${max_value}, \
seed=${seed}" | tee -a "$log_file"

    # 运行测试并写入 CSV（simple_payment 自身会 mkdir -p 父目录）。
    ${EXE} \
        -a ${n_acc} \
        -b ${load_batch_size} \
        -t ${num_txn} \
        -p ${tx_per_block} \
        -s ${seed} \
        -k ${key_size} \
        -m ${initial_balance_max} \
        -x ${max_value} \
        -d ${data_path} \
        -i ${index_path} \
        -r ${result_path} | tee -a "$log_file"

    sleep 5
    set +x
done