import os
import argparse
from itertools import product
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Process some CSV files.')
    parser.add_argument('db_name', type=str, help='The name of the tested db')
    parser.add_argument('test_name', type=str, help='The name of the test')
    args = parser.parse_args()

    detail_dir = f'results_{args.db_name}/{args.test_name}'
    summary_file = f'results_{args.db_name}/{args.test_name}_summary.csv'
    range_file = f'results_{args.db_name}/{args.test_name}_range.csv'
    summary_plot_file = f'results_{args.db_name}/{args.test_name}_summary.png'
    
    
    summary_dict = {"entry_count":[], "batch_size":[], "value_size":[], 
                    "load_latency":[], "get_latency":[], "put_latency":[],
                    "load_throughput":[], "get_throughput":[], "put_throughput":[]}
    range_dict = {"entry_count":[], "batch_size":[], "value_size":[], "range_size": [], 
                  "latency":[], "throughput":[]}
    detail_files = []
    for entry in os.listdir(detail_dir):
        full_path = os.path.join(detail_dir, entry)
        if os.path.isfile(full_path) and entry.endswith('.csv'):
            fname = full_path.split('/')[-1].split('.')[0]
            fname, vl = fname.split('v')
            vl = int(vl)
            acc, bz = fname.split('b')
            acc, bz = int(acc.strip('e')), int(bz)
            detail_files.append((acc, bz, vl, fname, full_path))
    detail_files = sorted(detail_files)
    for acc, bz, vl, fn, fp in detail_files:
        df = pd.read_csv(fp)
        load_df = df[df['operation'] == 'LOAD']
        put_df = df[df['operation'] == 'PUT']
        get_df = df[df['operation'] == 'GET']
        
        summary_dict["entry_count"].append(acc)
        summary_dict["batch_size"].append(bz)
        summary_dict["value_size"].append(vl)
        
        summary_dict["load_latency"].append(np.mean(load_df['latency'].to_numpy()))
        summary_dict["get_latency"].append(np.mean(get_df['latency'].to_numpy()))
        summary_dict["put_latency"].append(np.mean(put_df['latency'].to_numpy()))
        summary_dict["load_throughput"].append(np.mean(load_df['throughput'].to_numpy()))
        summary_dict["get_throughput"].append(np.mean(get_df['throughput'].to_numpy()))
        summary_dict["put_throughput"].append(np.mean(put_df['throughput'].to_numpy()))
        
    summary_df =  pd.DataFrame(summary_dict)
    summary_df.to_csv(summary_file, index=False)
    
    plt.figure(figsize=(9, 6))
    unique_v = summary_df['value_size'].unique()
    unique_a = summary_df['entry_count'].unique()
    
    # 控制subplot之间的间距
    plt.subplots_adjust(wspace=0.2, hspace=0.4)
    plt.subplot(2, 2, 1)
    for a, v in product(unique_a, unique_v):
        data = summary_df[np.logical_and(summary_df['entry_count'] == a, summary_df['value_size'] == v)]
        plt.plot(data['batch_size'].to_numpy(), data['get_latency'].to_numpy(), 
                marker='o', label=f'{a} entries, {v}B value')
    plt.ticklabel_format(style='sci', scilimits=(-1,2), axis='y')
    plt.ylim(0, None)
    plt.title('Get Latency vs Batch Size')
    plt.xlabel('Batch Size')
    plt.ylabel('Latency (s)')
    # plt.legend()
    plt.grid(True)
    
    plt.subplot(2, 2, 2)
    for a, v in product(unique_a, unique_v):
        data = summary_df[np.logical_and(summary_df['entry_count'] == a, summary_df['value_size'] == v)]
        plt.plot(data['batch_size'].to_numpy(), data['put_latency'].to_numpy(), 
                marker='o', label=f'{a} entries, {v}B value')
    plt.ticklabel_format(style='sci', scilimits=(-1,2), axis='y')
    plt.ylim(0, None)
    plt.title('Put Latency vs Batch Size')
    plt.xlabel('Batch Size')
    plt.ylabel('Latency (s)')
    # plt.legend()
    plt.grid(True)
    
    plt.subplot(2, 2, 3)
    for a, v in product(unique_a, unique_v):
        data = summary_df[np.logical_and(summary_df['entry_count'] == a, summary_df['value_size'] == v)]
        plt.plot(data['batch_size'].to_numpy(), data['get_throughput'].to_numpy(), 
                marker='o', label=f'{a} entries, {v}B value')
    plt.ticklabel_format(style='sci', scilimits=(-1,2), axis='y')
    plt.ylim(0, None)
    plt.title('Get Throughput vs Batch Size')
    plt.xlabel('Batch Size')
    plt.ylabel('Throughput (KOPS)')
    # plt.legend()
    plt.grid(True)
    
    plt.subplot(2, 2, 4)
    for a, v in product(unique_a, unique_v):
        data = summary_df[np.logical_and(summary_df['entry_count'] == a, summary_df['value_size'] == v)]
        plt.plot(data['batch_size'].to_numpy(), data['put_throughput'].to_numpy(), 
                marker='o', label=f'{a} entries, {v}B value')
    plt.ticklabel_format(style='sci', scilimits=(-1,2), axis='y')
    plt.ylim(0, None)
    plt.title('Put Throughput vs Batch Size')
    plt.xlabel('Batch Size')
    plt.ylabel('Throughput (KOPS)')
    plt.legend()
    plt.grid(True)
    
    plt.savefig(summary_plot_file, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"图表已保存为 {summary_plot_file}")

