#!/bin/bash
mode=release

# 定义测试参数数组
key_size=32  # 20:SHA-1, 32:SHA-256
batch_sizes=(100 200 300 400 500)
value_sizes=(1024)
n_test=5
data_path="$PWD/../data/"
index_path="$PWD/../index"
echo "data_path: $data_path"
echo "index_path: $index_path"


# 编译项目
cd ../
./build.sh --build-type ${mode} --cxx g++
# 创建结果目录
cd exps/
mkdir -p results
cd results
mkdir -p get_put_hashed_key_k${key_size}_${mode}
cd ..

# 运行测试
for batch_size in "${batch_sizes[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        set -x
        # 清理数据文件夹
        rm -rf $data_path
        mkdir -p $data_path
        rm -rf $index_path
        mkdir -p $index_path
        
        result_path="$PWD/results/get_put_hashed_key_k${key_size}_${mode}/b${batch_size}v${value_size}.csv"
        # 运行测试并提取结果
        ../build_${mode}/bin/get_put_hashed_key -b $batch_size -v $value_size -k $key_size -n $n_test -d $data_path -i $index_path -r $result_path > results/test_get_put_hashed_key_k$key_size_${mode}.log
        set +x
        sleep 5
    done
done

python3 plot.py get_put_hashed_key_k${key_size}_${mode}