db_name=$1
echo "db_name: $db_name"
# 定义测试参数数组
# load_account=(1000000 10000000)
load_account=(10000000)
# value_sizes=(256 512 1024 2048)
value_sizes=(1024)
ranges="5,50,100,200,300,400,500,1000,2000"
num_range_test=20
# load_batch_size=20000
load_batch_size=10000
key_size=64

data_path="$PWD/../data/"
index_path="$PWD/../index"
result_dir="$PWD/results_${db_name}/range_benchmark"
echo "data_path: $data_path"
echo "index_path: $index_path"
echo "result_dir: $result_dir"


mkdir -p $data_path
mkdir -p $index_path
mkdir -p ${result_dir}
# rm -rf ${result_dir}/*

# 运行测试
for n_acc in "${load_account[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        set -x
        # 清理数据文件夹
        rm -rf $data_path/*
        rm -rf $index_path/*
        
        result_path="${result_dir}/e${n_acc}b${batch_size}v${value_size}.csv"
        echo $(date "+%Y-%m-%d %H:%M:%S") 
        echo "num account: ${n_acc}, batch_size: ${batch_size}, value_size: ${value_size}, key_size: ${key_size}" 
        # 运行测试并提取结果
        ../build_release/bin/rangeBenchmark -a $n_acc -b $load_batch_size -t $num_range_test -k $key_size -v $value_size -l $ranges -d $data_path -i $index_path -r $result_path
        sleep 5
        set +x
    done
done