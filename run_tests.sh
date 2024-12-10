#!/bin/bash

# 编译项目
./build.sh

# 创建结果目录
mkdir -p results

# 定义测试参数数组
batch_sizes=(500 1000 2000 4000)
value_sizes=(256 512 1024 2048)

# 创建结果文件
echo "batch_size,value_size,put_latency,get_latency,put_throughput,get_throughput" > results/results.csv

# 运行测试
for batch_size in "${batch_sizes[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        echo "Running test with batch_size=$batch_size, value_size=$value_size"
        # 通过临时文件来修改代码中的参数
        sed -i "s/int batch_size = [0-9]*;/int batch_size = $batch_size;/" main.cpp
        sed -i "s/int value_len = [0-9]*;/int value_len = $value_size;/" main.cpp
        
        # 重新编译
        ./build.sh
        
        # 运行测试并提取结果
        output=$(./build/bin/get_put)
        
        # 使用awk提取平均延迟和吞吐量
        put_latency=$(echo "$output" | grep "average:" | awk '{print $3}')
        get_latency=$(echo "$output" | grep "average:" | awk '{print $6}')
        put_throughput=$(echo "$output" | grep "throughput:" | awk '{print $2}')
        get_throughput=$(echo "$output" | grep "throughput:" | awk '{print $4}')
        
        # 保存结果
        echo "$batch_size,$value_size,$put_latency,$get_latency,$put_throughput,$get_throughput" >> results/results.csv
    done
done