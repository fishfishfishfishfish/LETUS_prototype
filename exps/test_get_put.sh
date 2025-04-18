#!/bin/bash

# 编译项目
cd ../
./build.sh

# 创建结果目录
cd exps/
mkdir -p results
cd results
mkdir -p get_put
cd ..

# 定义测试参数数组
key_size=64
batch_sizes=(500 1000 2000 3000 4000 5000)
value_sizes=(256 512 1024 2048)
n_test=8
data_path="$PWD/../data/"
index_path="$PWD/../index"
echo "data_path: $data_path"
echo "index_path: $index_path"

# 创建结果文件
echo "batch_size,value_size,n_test,get_latency,put_latency,get_throughput,put_throughput" > results/get_put_results.csv

# 运行测试
for batch_size in "${batch_sizes[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        set -x
        # 清理数据文件夹
        rm -rf $data_path
        mkdir -p $data_path
        rm -rf $index_path
        mkdir -p $index_path
        
        result_path="$PWD/results/get_put/b${batch_size}v${value_size}.csv"
        echo $(date "+%Y-%m-%d %H:%M:%S") >> results/test_get_put_hashed_key_k${key_size}_${mode}.log
        echo "batch_size: ${batch_size}, value_size: ${value_size}, key_size: ${key_size}" >> results/test_get_put_hashed_key_k${key_size}_${mode}.log
        # 运行测试并提取结果
        ../build_release/bin/get_put -b $batch_size -k $key_size -v $value_size -n $n_test -d $data_path -i $index_path -r $result_path
        
        # 保存结果
        echo "$batch_size,$value_size,$n_test,$put_latency,$get_latency,$put_throughput,$get_throughput" >> results/get_put_results.csv
        sleep 5
        set +x
    done
done

python3 plot.py get_put