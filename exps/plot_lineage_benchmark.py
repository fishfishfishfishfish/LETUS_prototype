import os
import argparse
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
    
    
    summary_dict = {"entry_count":[], "value_size":[], "version_count":[], "latency":[], "throughput":[]}
    detail_files = []
    for entry in os.listdir(detail_dir):
        full_path = os.path.join(detail_dir, entry)
        if os.path.isfile(full_path) and entry.endswith('.csv'):
            fname = full_path.split('/')[-1].split('.')[0]
            acc, vl = fname.split('v')
            vl = int(vl)
            acc = int(acc.strip('e'))
            detail_files.append((acc, vl, fname, full_path))
    detail_files = sorted(detail_files)
    for acc, vl, fn, fp in detail_files:
        df = pd.read_csv(fp)
        df = df[df['operation'] == 'GET']
        for nv in df["version"].unique():
            summary_dict["entry_count"].append(acc)
            summary_dict["value_size"].append(vl)
            summary_dict["version_count"].append(nv)
            summary_dict["latency"].append(np.mean(df[df['version'] == nv]['latency'].to_numpy()))
            summary_dict["throughput"].append(np.mean(df[df['version'] == nv]['throughput'].to_numpy()))

    summary_df = pd.DataFrame(summary_dict)
    summary_df.to_csv(summary_file, index=False)