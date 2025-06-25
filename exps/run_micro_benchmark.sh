timestamp=$(date +"%Y%m%d_%H%M%S")
./micro_benchmark.sh letus "${timestamp}" > micro_benchmark_letus_${timestamp}.log 2>&1
python3 plot_micro_benchmark.py letus micro_benchmark_${timestamp}