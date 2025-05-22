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

summary_dict = {"batch_size":[], "value_size":[], 
                "get_latency":[], "put_latency":[],
                "get_throughput":[], "put_throughput":[]}
detail_files = []

print(f"Processing directory: {detail_dir}")
for entry in os.listdir(detail_dir):
    full_path = os.path.join(detail_dir, entry)
    if os.path.isfile(full_path) and entry.endswith('.csv'):
        print(f"Processing file: {entry}")
        fname = full_path.split('/')[-1].split('.')[0]
        print(f"Filename parts: {fname}")
        
        try:
            # 修改文件名解析逻辑
            if 'b' in fname and 'v' in fname:
                bz_part, vl_part = fname.split('v')
                bz = int(bz_part.strip('b'))
                vl = int(vl_part)
                print(f"Parsed batch_size: {bz}, value_size: {vl}")
                detail_files.append((bz, vl, fname, full_path))
            else:
                print(f"Skipping file {entry} - does not match expected format")
        except ValueError as e:
            print(f"Error parsing file {entry}: {e}")
            continue
            
detail_files = sorted(detail_files)
print(f"Found {len(detail_files)} valid files to process")

for bz, vl, fn, fp in detail_files:
    try:
        print(f"Processing file: {fp}")
        # 读取文件内容
        with open(fp, 'r') as f:
            content = f.read()
        
        # 分割获取summary部分
        if '---summary---' in content:
            summary_content = content.split('---summary---')[1].strip()
            # 使用StringIO来让pandas读取字符串
            from io import StringIO
            # 使用更宽松的解析方式
            df = pd.read_csv(StringIO(summary_content), 
                           on_bad_lines='skip',  # 跳过有问题的行
                           engine='python')      # 使用python引擎而不是C引擎
        else:
            print(f"Warning: No summary section found in {fp}")
            continue

        # 确保所有必需的列都存在
        required_columns = ['version', 'latency', 'put_latency', 
                          'get_throughput', 'put_throughput']
        if not all(col in df.columns for col in required_columns):
            print(f"Warning: Missing required columns in {fp}")
            print(f"Available columns: {df.columns.tolist()}")
            continue

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
        plt_f = f"{'.'.join(fp.split('.')[:-1])}.png"
        plt.savefig(plt_f, dpi=300, bbox_inches='tight')
        plt.close()
        
        # 使用summary部分的最后一行作为汇总数据
        last_row = df.iloc[-1]
        summary_dict["batch_size"].append(bz)
        summary_dict["value_size"].append(vl)
        summary_dict["get_latency"].append(float(last_row['latency']))
        summary_dict["put_latency"].append(float(last_row['put_latency']))
        summary_dict["get_throughput"].append(float(last_row['get_throughput']))
        summary_dict["put_throughput"].append(float(last_row['put_throughput']))
    except Exception as e:
        print(f"Error processing file {fp}: {e}")
        print(f"File content preview:\n{content[:200]}")  # 打印文件开头部分
        continue

# plot summary
df = pd.DataFrame(summary_dict)
print("\nSummary DataFrame:")
print(df)

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

plt.ylim(0, None)
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

plt.ylim(0, None)
plt.title('Put Throughput vs Batch Size')
plt.xlabel('Batch Size')
plt.ylabel('Throughput (KOPS)')
plt.legend()
plt.grid(True)

plt.tight_layout()

plt.savefig(summary_plot_file, dpi=300, bbox_inches='tight')
plt.close()

print(f"\n图表已保存为 {summary_plot_file}")