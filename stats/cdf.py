import os
import json
import argparse
import math
import statistics 
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

import numpy as np
from mpl_toolkits.axes_grid1.inset_locator import inset_axes, mark_inset

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

def extract_rtt_metrics(qlog_data):
    latest_rtts = []
    events = qlog_data['traces'][0]['events']
    for event in events:
        if event[1] == 'recovery' and event[2] == 'metric_update':
            latest_rtts.append(event[3].get('latest_rtt', None))

    latest_rtts_ms_filtered = [i/1000 for i in latest_rtts if i is not None]
    return latest_rtts_ms_filtered

def get_all_qlog_files(directory, extensions=('.qlog')):
    if not os.path.isdir(directory):
        raise ValueError(f"{directory} is not a valid directory.")
    files = [os.path.join(directory, f) for f in os.listdir(directory)
             if os.path.isfile(os.path.join(directory, f)) and f.lower().endswith(extensions)]
    files = sorted(files, key=os.path.getmtime)
    return files[-40:]

def build_rtt(sub, args):
    rtt_data = []
    sub_path = os.path.join(args.parent_dir, sub)
    try:
        qlog_files = get_all_qlog_files(sub_path)
    except ValueError as e:
        print(f"Error in subdirectory {sub_path}: {e}")
        exit(1)
    for file in qlog_files:
        with open(file , 'r') as f:
            qlog_data = json.load(f)
        rtt_data.extend(extract_rtt_metrics(qlog_data))
    return rtt_data

def cdf(data):
    sorted_data = np.sort(data)
    cdf_values = np.arange(1, len(sorted_data) + 1) / len(sorted_data)
    return sorted_data, cdf_values

def main():
    parser = argparse.ArgumentParser(description='Process qlog files and plot metrics for comparison.')
    parser.add_argument('--parent-dir', type=str, help="Parent directory containing subdirectories with qlog files.")
    args = parser.parse_args()
    
    line_width = 2.5
    plt.figure(figsize=(10, 7))

    westwood_rtt = build_rtt('westwood+', args)
    qdc20_rtt = build_rtt('delay_control_20', args)
    qdc50_rtt = build_rtt('delay_control_50', args)
    cubic_rtt = build_rtt('cubic', args)
    bbr2_rtt = build_rtt('bbr2', args)
    newreno_rtt = build_rtt('newreno', args)

    avg_westwood = np.mean(westwood_rtt)
    std_westwood = np.std(westwood_rtt)
    avg_qdc20 = np.mean(qdc20_rtt)
    std_qdc20 = np.std(qdc20_rtt)
    avg_qdc50 = np.mean(qdc50_rtt)
    std_qdc50 = np.std(qdc50_rtt)
    avg_cubic = np.mean(cubic_rtt)
    std_cubic = np.std(cubic_rtt)
    avg_bbr2 = np.mean(bbr2_rtt)
    std_bbr2 = np.std(bbr2_rtt)
    avg_newreno = np.mean(newreno_rtt)
    std_newreno = np.std(newreno_rtt)

    sorted_data, cdf_values = cdf(qdc20_rtt)
    plt.plot(sorted_data, cdf_values, label='QUIC-DC (20%)', color="#377eb8")

    sorted_data, cdf_values = cdf(qdc50_rtt)
    plt.plot(sorted_data, cdf_values, label='QUIC-DC (50%)', color="#4daf4a")

    sorted_data, cdf_values = cdf(westwood_rtt)
    plt.plot(sorted_data, cdf_values, label='Westwood+', color="#ff7f00")

    sorted_data, cdf_values = cdf(bbr2_rtt)
    plt.plot(sorted_data, cdf_values, label='BBRv2', color="#a65628")

    sorted_data, cdf_values = cdf(cubic_rtt)
    plt.plot(sorted_data, cdf_values, label='Cubic', color="#f781bf")

    sorted_data, cdf_values = cdf(newreno_rtt)
    plt.plot(sorted_data, cdf_values, label='New Reno', color="#17becf")

    plt.xlabel('RTT (ms)', fontsize=22)
    plt.ylabel('CDF',fontsize=22)
    plt.xlim(30,100)

    plt.grid(True)

    plt.legend(fontsize=18)

    plt.gca().tick_params(axis='both', labelsize=16)

    print("Salvando il grafico come cdf.pdf...")
    plt.savefig("cdf.pdf", bbox_inches="tight")
    print("Grafico salvato con successo!")
    print(f"Westwood RTT medio: {avg_westwood}. Deviazione standard: {std_westwood}")
    print(f"QUIC-DC (20%) RTT medio: {avg_qdc20}. Deviazione standard: {std_qdc20}")
    print(f"QUIC-DC (50%) RTT medio: {avg_qdc50}. Deviazione standard: {std_qdc50}")
    print(f"Cubic RTT medio: {avg_cubic}. Deviazione standard: {std_cubic}")
    print(f"BBRv2 RTT medio: {avg_bbr2}. Deviazione standard: {std_bbr2}")
    print(f"New Reno RTT medio: {avg_newreno}. Deviazione standard: {std_newreno}")

if __name__ == '__main__':
    main()
