import os
import argparse
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description='Process some CSV files.')
parser.add_argument('test_name', type=str, help='The name of the test')
args = parser.parse_args()

detail_dir = f'results/{args.test_name}'
summary_file = f'results/{args.test_name}_results.csv'
summary_plot_file = f'results/{args.test_name}_results.png'

detail_files = []
for entry in os.listdir(detail_dir):
    full_path = os.path.join(detail_dir, entry)
    if os.path.isfile(full_path) and entry.endswith('.csv'):
        detail_files.append(full_path)
for f in detail_files:
    df = pd.read_csv(f)
    plt.figure(figsize=(15, 10))
    plt.plot(df['version'].to_numpy(), df['get_throughput'].to_numpy()/1000, 
                marker='o', label=f'get throughput')
    plt.plot(df['version'].to_numpy(), df['put_throughput'].to_numpy()/1000, 
                marker='o', label=f'put throughput')
    plt.title('Throughput as scaling')
    plt.xlabel('version')
    plt.ylabel('throughput (KOPS)')
    plt.legend()
    plt.grid(True)
    plt_f = f"{'.'.join(f.split('.')[:-1])}.png"
    plt.savefig(plt_f, dpi=300, bbox_inches='tight')
    plt.close()

# plot summary
df = pd.read_csv(summary_file)

plt.figure(figsize=(9, 6))

plt.subplot(2, 2, 1)
for value_size in sorted(df['value_size'].unique()):
    data = df[df['value_size'] == value_size]
    plt.plot(data['batch_size'].to_numpy(), data['get_latency'].to_numpy(), 
             marker='o', label=f'Value Size={value_size}B')

plt.title('Get Latency vs Batch Size')
plt.xlabel('Batch Size')
plt.ylabel('Latency (s)')
plt.legend()
plt.grid(True)

plt.subplot(2, 2, 2)
for value_size in sorted(df['value_size'].unique()):
    data = df[df['value_size'] == value_size]
    plt.plot(data['batch_size'].to_numpy(), data['put_latency'].to_numpy(), 
             marker='o', label=f'Value Size={value_size}B')

plt.title('Put Latency vs Batch Size')
plt.xlabel('Batch Size')
plt.ylabel('Latency (s)')
plt.legend()
plt.grid(True)

plt.subplot(2, 2, 3)
for value_size in sorted(df['value_size'].unique()):
    data = df[df['value_size'] == value_size]
    plt.plot(data['batch_size'].to_numpy(), data['get_throughput'].to_numpy()/1000, 
             marker='o', label=f'Value Size={value_size}B')

plt.title('Get Throughput vs Batch Size')
plt.xlabel('Batch Size')
plt.ylabel('Throughput (KOPS)')
plt.legend()
plt.grid(True)

plt.subplot(2, 2, 4)
for value_size in sorted(df['value_size'].unique()):
    data = df[df['value_size'] == value_size]
    plt.plot(data['batch_size'].to_numpy(), data['put_throughput'].to_numpy()/1000, 
             marker='o', label=f'Value Size={value_size}B')

plt.title('Put Throughput vs Batch Size')
plt.xlabel('Batch Size')
plt.ylabel('Throughput (KOPS)')
plt.legend()
plt.grid(True)

plt.tight_layout()

plt.savefig(summary_plot_file, dpi=300, bbox_inches='tight')
plt.close()

print(f"图表已保存为 {summary_plot_file}")