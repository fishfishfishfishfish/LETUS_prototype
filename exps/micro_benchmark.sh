db_name=$1
test_name=$2
echo "db_name: $db_name, test_name=$test_name"
# 定义测试参数数组
# load_account=(1000000)
load_account=(40000000)
# load_account=(100000000)
# batch_sizes=(500 1000 2000 3000 4000 5000)
# value_sizes=(256 512 1024 2048)
num_transaction_version=20
load_batch_size=10000
# load_batch_size=100000
key_size=32

data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/micro_benchmark_${test_name}"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"

mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}
# rm -rf ${result_dir}/*
echo "entry_count,batch_size,value_size,key_size,data_folder_size,index_folder_size,folder_size" > "${result_dir}/size"    
# 运行测试
for n_acc in "${load_account[@]}"; do
    for batch_size in "${batch_sizes[@]}"; do
        for value_size in "${value_sizes[@]}"; do
            set -x
            # 清理数据文件夹
            rm -rf $data_path/*
            rm -rf $index_path/*

            
            result_path="${result_dir}/e${n_acc}b${batch_size}v${value_size}.csv"
            echo $(date "+%Y-%m-%d %H:%M:%S") 
            echo "num account: ${n_acc}, batch_size: ${batch_size}, value_size: ${value_size}, key_size: ${key_size}" 
            # 运行测试并提取结果
            ../build_release/bin/microBenchmark -a $n_acc -b $load_batch_size -t $num_transaction_version -z $batch_size -k $key_size -v $value_size -d $data_path -i $index_path -r $result_path
            sleep 5
            set +x

            # 检查文件夹是否存在
            if [ ! -d "$data_path" ]; then
                echo "数据文件夹 $data_path 不存在。"
                exit 1
            fi
            # 检查文件夹是否存在
            if [ ! -d "$index_path" ]; then
                echo "数据文件夹 $data_path 不存在。"
                exit 1
            fi
            # 获取文件夹大小
            data_folder_size=$(du -sk "$data_path" | cut -f1)
            index_folder_size=$(du -sk "$index_path" | cut -f1)
            # 输出结果
            echo "${n_acc},${batch_size},${value_size},${key_size},${data_folder_size},${index_folder_size},${data_folder_size+index_folder_size}" >> "${result_dir}/size"  
        done
    done
done