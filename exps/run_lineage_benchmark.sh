timestamp=$(date +"%Y%m%d_%H%M%S")
./lineage_benchmark.sh letus "${timestamp}" > lineage_benchmark_letus_${timestamp}.log  2>&1
python3 plot_lineage_benchmark.py letus lineage_benchmark_${timestamp}