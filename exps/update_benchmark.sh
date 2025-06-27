db_name=$1
test_name=$2
echo "db_name: $db_name, test_name=$test_name"
# 定义测试参数数组
load_account=(500 1000 2000 3000 4000 5000)
value_sizes=(1024)
key_size=64
update_count=100

# data_path="$PWD/../data/"
# result_dir="$PWD/results_${db_name}/update_benchmark"
# echo "data_path: $data_path"
# echo "result_dir: $result_dir"


data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/update_benchmark"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"

mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}

# 运行测试
for n_acc in "${load_account[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        set -x
        # 清理数据文件夹
        rm -rf $data_path/*
        rm -rf $index_path/*
        
        result_path="${result_dir}/e${n_acc}u${update_count}v${value_size}.csv"
        echo $(date "+%Y-%m-%d %H:%M:%S") 
        echo "num account: ${n_acc}, update count:${update_count}, value_size: ${value_size}, key_size: ${key_size}" 
        # 运行测试并提取结果
        ../build_release/bin/updateBenchmark -a $n_acc -t $update_count -k $key_size -v $value_size -d $data_path -r $result_path
        sleep 5
        set +x
    done
done