import os
import json
import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

def get_linux_fps(directory):
    summary_files = glob.glob(f"{directory}/*summary.csv")
    if not summary_files:
        return 0.0
    df = pd.read_csv(summary_files[0])
    return float(df['Average FPS'].iloc[0])

def get_windows_fps(filepath):
    with open(filepath, encoding="utf-8-sig") as f:
        data = json.load(f)
    ms = data['Runs'][0]['CaptureData']['MsBetweenPresents']
    return 1000.0 / (sum(ms) / len(ms))

def main():
    # Gather data
    omen_linux_off = get_linux_fps("OMEN15-Linux-OFF")
    omen_linux_on = get_linux_fps("OMEN15-Linux-ON")
    uni_linux_off = get_linux_fps("UNI-Linux-OFF-unlimited")
    uni_linux_on = get_linux_fps("UNI-Linux-ON-unlimited")
    
    omen_win_off_files = glob.glob("*OMEN15-Win-OFF*.json")
    omen_win_on_files = glob.glob("*OMEN15-Win-ON*.json")
    
    omen_win_off = get_windows_fps(omen_win_off_files[0]) if omen_win_off_files else 0.0
    omen_win_on = get_windows_fps(omen_win_on_files[0]) if omen_win_on_files else 0.0

    print("Parsed Data:")
    print(f"OMEN15 Linux OFF: {omen_linux_off:.2f}")
    print(f"OMEN15 Linux ON: {omen_linux_on:.2f}")
    print(f"UNI Linux OFF: {uni_linux_off:.2f}")
    print(f"UNI Linux ON: {uni_linux_on:.2f}")
    print(f"OMEN15 Win OFF: {omen_win_off:.2f}")
    print(f"OMEN15 Win ON: {omen_win_on:.2f}")

    # Plot 1: OS Comparison (OMEN15)
    labels = ['Frustum Culling OFF', 'Frustum Culling ON']
    win_means = [omen_win_off, omen_win_on]
    lin_means = [omen_linux_off, omen_linux_on]

    x = np.arange(len(labels))
    width = 0.35

    fig, ax = plt.subplots(figsize=(8, 6))
    rects1 = ax.bar(x - width/2, win_means, width, label='Windows 11', color='skyblue', edgecolor='black')
    rects2 = ax.bar(x + width/2, lin_means, width, label='Ubuntu 24.04', color='salmon', edgecolor='black')

    ax.set_ylabel('Average FPS')
    ax.set_title('OS Comparison (OMEN15 Laptop)\nWindows 11 vs Ubuntu 24.04')
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis='y', linestyle='--', alpha=0.7)

    for rects in [rects1, rects2]:
        for rect in rects:
            height = rect.get_height()
            ax.annotate(f'{height:.1f}',
                        xy=(rect.get_x() + rect.get_width() / 2, height),
                        xytext=(0, 3),
                        textcoords="offset points",
                        ha='center', va='bottom')

    plt.tight_layout()
    plt.savefig('Chart1_OS_Comparison.png', dpi=300)
    plt.close()

    # Plot 2: Hardware Scaling (Linux)
    omen_means = [omen_linux_off, omen_linux_on]
    uni_means = [uni_linux_off, uni_linux_on]

    fig, ax = plt.subplots(figsize=(8, 6))
    rects1 = ax.bar(x - width/2, omen_means, width, label='OMEN15 (GTX 1660 Ti)', color='lightgreen', edgecolor='black')
    rects2 = ax.bar(x + width/2, uni_means, width, label='UNI PC (RTX 4070)', color='orchid', edgecolor='black')

    ax.set_ylabel('Average FPS')
    ax.set_title('Hardware Scaling (Linux)\nGTX 1660 Ti vs RTX 4070')
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis='y', linestyle='--', alpha=0.7)

    for rects in [rects1, rects2]:
        for rect in rects:
            height = rect.get_height()
            ax.annotate(f'{height:.1f}',
                        xy=(rect.get_x() + rect.get_width() / 2, height),
                        xytext=(0, 3),
                        textcoords="offset points",
                        ha='center', va='bottom')

    plt.tight_layout()
    plt.savefig('Chart2_Hardware_Scaling.png', dpi=300)
    plt.close()

if __name__ == '__main__':
    main()
