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

def extract_rtt_metrics(qlog_data):
    """
    Extract RTT data from qlog_data.

    Returns latest_rtts_ms_filtered (list of float): Latest RTT in milliseconds withoute None values 
    """
    latest_rtts = []

    events = qlog_data['traces'][0]['events']
    for event in events:
        if event[1] == 'recovery' and event[2] == 'metric_update':
            latest_rtts.append(event[3].get('latest_rtt', None))

    latest_rtts_ms_filtered = [i/1000 for i in latest_rtts if i is not None]
    return latest_rtts_ms_filtered

def get_all_qlog_files(directory, extensions=('.qlog')):
    """
    Returns the last n qlog files (sorted by modification time) in the given directory.
    """
    if not os.path.isdir(directory):
        raise ValueError(f"{directory} is not a valid directory.")
    files = [os.path.join(directory, f) for f in os.listdir(directory)
             if os.path.isfile(os.path.join(directory, f)) and f.lower().endswith(extensions)]
    files = sorted(files, key=os.path.getmtime)
    return files

def build_rtt(sub):
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

def cdf(data):
    sorted_data = np.sort(data)
    cdf_values = np.arange(1, len(sorted_data) + 1)/len(sorted_data)
    return sorted_data, cdf_values

def main():
    parser = argparse.ArgumentParser(description='Process qlog files and plot metrics for comparison.')
    parser.add_argument('--parent-dir', type=str,
                       help="Parent directory containing subdirectories 'westwood+', 'delay_control_20', 'delay_control_50', 'cubic', and 'bbr2'.\n"
                            "From each, all the qlog files are selected.")
    westwood_rtt = build_rtt('westwood+')
    qdc20_rtt = build_rtt('delay_control_20')
    qdc50_rtt = build_rtt('delay_control_50')
    cubic_rtt = build_rtt('cubic')
    bbr2_rtt = build_rtt('bbr2')

    sorted_data, cdf_values = cdf(westwood_rtt)
    plt.plot(sorted_data, cdf_values, label='Westwood+', color='red')

    sorted_data, cdf_values = cdf(qdc20_rtt)
    plt.plot(sorted_data, cdf_values, label='Quidc Delay Control 20%', color='blue')

    sorted_data, cdf_values = cdf(qdc50_rtt)
    plt.plot(sorted_data, cdf_values, label='Quic Delay Control 50%', color='green')

    sorted_data, cdf_values = cdf(cubic_rtt)
    plt.plot(sorted_data, cdf_values, label='Cubic', color='fuchsia')

    sorted_data, cdf_values = cdf(bbr2_rtt)
    plt.plot(sorted_data, cdf_values, label='BBRv2', color='aquamarine')

    plt.set_title('CDF of RTT')
    plt.set_xlabel('RTT (ms)')
    plt.set_ylabel('CDF')

    plt.legend()

    plt.savefig("cdf.png")



            