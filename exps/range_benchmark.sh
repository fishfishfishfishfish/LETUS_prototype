source env.sh

# 定义测试参数数组
load_account=(10000000)
value_sizes=(256 512 1024 2048)
ranges="5,50,100,200,300,400,500,1000,2000"
num_range_test=20
load_batch_size=10000
key_size=64
COOLDOWN_TIME=5  # 测试间隔时间

data_path="${DB_DIR}/data/"
index_path="${DB_DIR}/index"
result_dir="${RES_DIR}/range_benchmark_${TIMESTAMP}"
log_file="${LOG_DIR}/range_benchmark_letus_${TIMESTAMP}.log"

# 日志初始化
init_logging() {
    mkdir -p "$LOG_DIR"
    {
        echo "=== ${DB_NAME} Range Benchmark 运行日志 ==="
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
# 运行测试
total_tests=$(( ${#load_account[@]} * ${#value_sizes[@]} ))
current_test=0
for n_acc in "${load_account[@]}"; do
    for value_size in "${value_sizes[@]}"; do
        current_test=$((current_test + 1))
        print_info "=== 开始测试 $current_test/$total_tests, $(date "+%Y-%m-%d %H:%M:%S")==="
        print_info "参数: n_acc=$n_acc, batch_size: ${load_batch_size}, ranges=$ranges, value_size=$value_size, key_size: ${key_size}"

        print_info "清理数据文件夹..."
        log_command "rm -rf $data_path/*" "清理数据文件夹"
        log_command "rm -rf $index_path/*" "清理索引文件夹"
        
        result_path="${result_dir}/e${n_acc}b${load_batch_size}v${value_size}.csv" 
        # 运行测试并提取结果
        print_info "启动 rangeBenchmark 程序..."
        benchmark_cmd="${BUILD_DIR}/bin/rangeBenchmark -a $n_acc -b $load_batch_size -t $num_range_test -k $key_size -v $value_size -l $ranges -d $data_path -i $index_path -r $result_path"
        log_command "$benchmark_cmd" "运行 rangeBenchmark 程序"

        print_info "等待 ${COOLDOWN_TIME} 秒冷却..."
        sleep $COOLDOWN_TIME
    done
done