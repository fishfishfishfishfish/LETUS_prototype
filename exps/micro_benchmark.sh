db_name=$1
echo "db_name: $db_name"
# 定义测试参数数组
load_account=(1000000)
# load_account=(40000000)
# load_account=(100000000)
batch_sizes=(500 1000 2000 3000 4000 5000)
value_sizes=(256 512 1024 2048)
num_transaction_version=20
load_batch_size=10000
# load_batch_size=100000
key_size=32

data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/micro_benchmark"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"


mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}
# rm -rf ${result_dir}/*

# 运行测试
for n_acc in "${load_account[@]}"; do
    for batch_size in "${batch_sizes[@]}"; do
        for value_size in "${value_sizes[@]}"; do
            set -x
            # 清理数据文件夹
            rm -rf $data_path/*
            
            result_path="${result_dir}/e${n_acc}b${batch_size}v${value_size}.csv"
            echo $(date "+%Y-%m-%d %H:%M:%S") 
            echo "num account: ${n_acc}, batch_size: ${batch_size}, value_size: ${value_size}, key_size: ${key_size}" 
            # 运行测试并提取结果
            ../build_release/bin/microBenchmark -a $n_acc -b $load_batch_size -t $num_transaction_version -z $batch_size -k $key_size -v $value_size -d $data_path -i $index_path -r $result_path
            sleep 5
            set +x
        done
    done
done