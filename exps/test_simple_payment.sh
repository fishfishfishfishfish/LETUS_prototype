#!/bin/bash
export ASAN_OPTIONS=detect_leaks=0
# 编译项目
cd ../
./build.sh


# 定义测试参数数组
db_name=$1
test_name=$2
echo "db_name: $db_name, test_name=$test_name"
# num_account=(50000000 100000000 250000000 500000000)
num_account=(50000000)
load_batch_size=100000
batch_size=4000
key_size=64
num_txn=80000

cd exps/
data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/simple-payment_${test_name}"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"

mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}
rm -rf ${result_dir}/*

# 运行测试
for n_acc in "${num_account[@]}"; do
    set -x
    # 清理数据文件夹
    rm -rf $data_path
    mkdir -p $data_path
    rm -rf $index_path
    mkdir -p $index_path
    
    result_path="${result_dir}/acc_${n_acc}.csv"
    echo $(date "+%Y-%m-%d %H:%M:%S")
    echo "batch_size: ${batch_size}, value_size: ${value_size}, key_size: ${key_size}"
    # 运行测试并提取结果
    ../build_release/bin/simple_payment -a $n_acc -b $load_batch_size -t $num_txn -z $batch_size -k $key_size -d $data_path -i $index_path -r $result_path
    
    sleep 5
    set +x
done