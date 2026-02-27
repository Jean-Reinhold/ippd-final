#!/usr/bin/env python3
"""Generate a markdown table and a plot from benchmark results.

Usage:
    python scripts/plot_bench.py results.csv

Output:
    - prints markdown table to stdout
    - saves plot to bench_plot.png
"""
import sys
import csv
import matplotlib.pyplot as plt

def read_results(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                'NP': int(r['NP']),
                'Threads': int(r['Threads']),
                'Time': float(r['Time']),
            })
    return rows


def print_markdown(rows, base_time):
    print('| NP | Threads | Time (s) | Speedup |')
    print('|----|---------|----------|---------|')
    for r in rows:
        speed = base_time / r['Time']
        print(f"| {r['NP']} | {r['Threads']} | {r['Time']:.3f} | {speed:.3f} |")


def plot(rows):
    # create a table of times per configuration
    fig, ax = plt.subplots()
    combos = [(r['NP'], r['Threads']) for r in rows]
    times = [r['Time'] for r in rows]
    labels = [f"{np}×{th}" for np,th in combos]
    ax.bar(labels, times)
    ax.set_ylabel('Time (s)')
    ax.set_title('Benchmark times')
    plt.xticks(rotation=45)
    plt.tight_layout()
    fig.savefig('bench_plot.png')
    print("plot saved to bench_plot.png")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    rows = read_results(sys.argv[1])
    base = next(r['Time'] for r in rows if r['NP']==1 and r['Threads']==1)
    print_markdown(rows, base)
    plot(rows)

if __name__ == '__main__':
    main()
