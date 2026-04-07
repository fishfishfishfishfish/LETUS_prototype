source env.sh

# 定义测试参数数组
load_account=(1000000)
batch_sizes=(500 1000 2000 3000 4000 5000)
value_sizes=(256 512 1024 2048)
num_transaction_version=20
load_batch_size=10000
key_size=64
COOLDOWN_TIME=5  # 测试间隔时间

data_path="${DB_DIR}/data/"
index_path="${DB_DIR}/index"
result_dir="${RES_DIR}/micro_benchmark_${TIMESTAMP}"
log_file="${LOG_DIR}/micro_benchmark_letus_${TIMESTAMP}.log"

# 日志初始化
init_logging() {
    mkdir -p "$LOG_DIR"
    {
        echo "=== ${DB_NAME} Micro Benchmark 运行日志 ==="
        echo "开始时间: $(date)"
        echo "脚本目录: $SCRIPT_DIR"
        echo "输出目录: $result_dir"
        echo "数据目录: $data_path"
        echo "索引目录: $index_path"
        echo "日志目录: $log_file"
        echo "========================================"
        echo ""
    } > "$log_file"
}

log_message() {
    local level="$1"
    local message="$2"
    local timestamp="$(date '+%Y-%m-%d %H:%M:%S')"
    
    echo "[$timestamp] [$level] $message" >> "$log_file"
}

# 打印带颜色的消息并记录到日志
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
    log_message "INFO" "$1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
    log_message "INFO" "$1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
    log_message "WARNING" "$1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    log_message "ERROR" "$1"
}

# 记录命令执行并捕获输出
log_command() {
    local cmd="$1"
    local description="$2"
    
    print_info "执行命令: $description"
    echo "COMMAND" "$cmd"
    log_message "COMMAND" "$cmd"
    
    # 执行命令并记录输出
    {
        echo "=== 命令输出开始 ==="
        eval "$cmd"
        local exit_code=$?
        echo "=== 命令输出结束 (退出码: $exit_code) ==="
        return $exit_code
    } | tee -a "$log_file"
    
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        print_error "命令执行失败: $description (退出码: $exit_code)"
    else
        print_success "命令执行成功: $description"
    fi
    return $exit_code
}

# 创建测试目录
create_test_dir() {
    local db_dir="${1:-${DB_DIR}}"
    local clean_db="${2:-$CLEAN_DB}"
    
    if [[ ! -d "$db_dir" ]]; then
        print_info "创建数据库目录: $db_dir"
        mkdir -p "$db_dir"
    fi
    
    # 清理旧数据（可选）
    if [[ "$clean_db" == "true" ]]; then
        print_warning "清理 ${db_dir} 旧数据..."
        rm -rf "$db_dir"/*
    fi
}

init_logging
create_test_dir ${data_path} true
create_test_dir ${index_path} true
create_test_dir ${result_dir} true
echo "entry_count,batch_size,value_size,key_size,data_folder_size,index_folder_size,folder_size" > "${result_dir}/size"
# 运行测试
total_tests=$(( ${#load_account[@]} * ${#batch_sizes[@]} * ${#value_sizes[@]} ))
current_test=0
# 运行测试
for n_acc in "${load_account[@]}"; do
    for batch_size in "${batch_sizes[@]}"; do
        for value_size in "${value_sizes[@]}"; do
            current_test=$((current_test + 1))
            print_info "=== 开始测试 $current_test/$total_tests, $(date "+%Y-%m-%d %H:%M:%S")==="
            print_info "参数: n_acc=$n_acc, batch_size=$batch_size, value_size=$value_size, key_size: ${key_size}"

            print_info "清理数据文件夹..."
            log_command "rm -rf $data_path/*" "清理数据文件夹"
            log_command "rm -rf $index_path/*" "清理索引文件夹"

            result_path="${result_dir}/e${n_acc}b${batch_size}v${value_size}.csv"

            # 运行测试程序
            print_info "启动 microBenchmark 程序..."
            benchmark_cmd="${BUILD_DIR}/bin/microBenchmark -a $n_acc -b $load_batch_size -t $num_transaction_version -z $batch_size -k $key_size -v $value_size -d $data_path -i $index_path -r $result_path"
            log_command "$benchmark_cmd" "执行 microBenchmark 测试"
            
            # 等待程序完全退出
            print_info "等待程序完全退出..."

            # 检查文件夹是否存在
            if [ ! -d "$data_path" ]; then
                print_error "数据文件夹 $data_path 不存在。"
                exit 1
            fi
            # 检查文件夹是否存在
            if [ ! -d "$index_path" ]; then
                print_error "索引文件夹 $index_path 不存在。"
                exit 1
            fi
            # 获取文件夹大小
            print_info "计算文件夹大小..."
            data_folder_size=$(du -sk "$data_path" | cut -f1)
            index_folder_size=$(du -sk "$index_path" | cut -f1)
            total_size=$((data_folder_size + index_folder_size))
            # 输出结果
            echo "${n_acc},${batch_size},${value_size},${key_size},${data_folder_size},${index_folder_size},${total_size}" >> "${result_dir}/size"  
            print_info "数据文件夹大小: ${data_folder_size} KB"
            print_info "索引文件夹大小: ${index_folder_size} KB"
            print_info "总大小: ${total_size} KB"
            print_success "测试 $current_test/$total_tests 完成"

            print_info "等待 ${COOLDOWN_TIME} 秒冷却..."
            sleep $COOLDOWN_TIME
        done
    done
done