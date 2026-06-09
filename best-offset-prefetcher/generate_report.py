#!/usr/bin/env python3
"""BOP Prefetcher Evaluation Report Generator"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib
matplotlib.rcParams['font.size'] = 11
matplotlib.rcParams['figure.dpi'] = 150

CSV_PATH = "3-workload.csv"
OUTPUT_DIR = "."

df = pd.read_csv(CSV_PATH)
df = df[df['config'] != 'nextline']  # segfaulted, exclude

configs = ['no_pref', 'stream', 'stride', 'bop']
config_labels = {'no_pref': 'No Prefetch', 'stream': 'Stream', 'stride': 'Stride', 'bop': 'BOP'}
config_colors = {'no_pref': '#888888', 'stream': '#4C72B0', 'stride': '#DD8452', 'bop': '#C44E52'}

workloads = df['workload'].unique()
workloads_short = [w.split('.')[1].replace('_r', '') for w in workloads]

# ── Figure 1: IPC comparison (grouped bar) ──
fig, ax = plt.subplots(figsize=(14, 5))
x = np.arange(len(workloads))
width = 0.2
for i, cfg in enumerate(configs):
    vals = []
    for wl in workloads:
        row = df[(df['workload'] == wl) & (df['config'] == cfg)]
        vals.append(row['ipc'].values[0] if len(row) else 0)
    bars = ax.bar(x + i * width, vals, width, label=config_labels[cfg], color=config_colors[cfg])
ax.set_ylabel('IPC')
ax.set_title('IPC Comparison Across Workloads and Prefetcher Configurations')
ax.set_xticks(x + 1.5 * width)
ax.set_xticklabels(workloads_short, rotation=30, ha='right')
ax.legend()
ax.grid(axis='y', alpha=0.3)
plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig1_ipc_comparison.png')
plt.close()

# ── Figure 2: Speedup vs no_pref ──
fig, ax = plt.subplots(figsize=(14, 5))
for i, cfg in enumerate(['stream', 'stride', 'bop']):
    vals = []
    for wl in workloads:
        row = df[(df['workload'] == wl) & (df['config'] == cfg)]
        vals.append(row['speedup_vs_no_pref'].values[0] if len(row) else 1.0)
    ax.bar(x + i * 0.25, vals, 0.25, label=config_labels[cfg], color=config_colors[cfg])
ax.axhline(y=1.0, color='black', linestyle='--', linewidth=0.8, alpha=0.5)
ax.set_ylabel('Speedup vs No Prefetch')
ax.set_title('Speedup Over Baseline (No Prefetch = 1.0)')
ax.set_xticks(x + 0.25)
ax.set_xticklabels(workloads_short, rotation=30, ha='right')
ax.legend()
ax.grid(axis='y', alpha=0.3)
plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig2_speedup.png')
plt.close()

# ── Figure 3: Prefetch accuracy & coverage ──
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

for ax_i, (metric, title) in enumerate([
    ('pref_accuracy', 'Prefetch Accuracy'),
    ('pref_coverage_vs_no_pref', 'Prefetch Coverage (vs No Prefetch)')
]):
    ax = axes[ax_i]
    for i, cfg in enumerate(['stream', 'stride', 'bop']):
        vals = []
        for wl in workloads:
            row = df[(df['workload'] == wl) & (df['config'] == cfg)]
            vals.append(row[metric].values[0] if len(row) else 0)
        ax.bar(x + i * 0.25, vals, 0.25, label=config_labels[cfg], color=config_colors[cfg])
    ax.set_ylabel(title)
    ax.set_title(title)
    ax.set_xticks(x + 0.25)
    ax.set_xticklabels(workloads_short, rotation=30, ha='right')
    ax.legend(fontsize=9)
    ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig3_accuracy_coverage.png')
plt.close()

# ── Figure 4: Cache miss reduction ──
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

for ax_i, (metric, title) in enumerate([
    ('dcache_miss_reduction_vs_no_pref', 'L1 D-Cache Miss Reduction'),
    ('mlc_miss_reduction_vs_no_pref', 'MLC (L2) Miss Reduction')
]):
    ax = axes[ax_i]
    for i, cfg in enumerate(['stream', 'stride', 'bop']):
        vals = []
        for wl in workloads:
            row = df[(df['workload'] == wl) & (df['config'] == cfg)]
            vals.append(row[metric].values[0] * 100 if len(row) else 0)
        ax.bar(x + i * 0.25, vals, 0.25, label=config_labels[cfg], color=config_colors[cfg])
    ax.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
    ax.set_ylabel(f'{title} (%)')
    ax.set_title(title)
    ax.set_xticks(x + 0.25)
    ax.set_xticklabels(workloads_short, rotation=30, ha='right')
    ax.legend(fontsize=9)
    ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig4_cache_miss_reduction.png')
plt.close()

# ── Figure 5: BOP-specific metrics ──
bop_df = df[df['config'] == 'bop'].copy()
bop_df['wl_short'] = [w.split('.')[1].replace('_r', '') for w in bop_df['workload']]

fig, axes = plt.subplots(1, 3, figsize=(16, 5))

ax = axes[0]
ax.bar(bop_df['wl_short'], bop_df['bop_pref_issued'], color=config_colors['bop'])
ax.set_ylabel('Count')
ax.set_title('BOP Prefetches Issued')
ax.tick_params(axis='x', rotation=30)
ax.grid(axis='y', alpha=0.3)

ax = axes[1]
ax.bar(bop_df['wl_short'], bop_df['bop_rr_hit_score_inc'], color='#55A868', label='RR Hit Score Inc')
ax.bar(bop_df['wl_short'], bop_df['bop_rr_fill_updates'], color='#4C72B0', alpha=0.5, label='RR Fill Updates')
ax.set_ylabel('Count')
ax.set_title('BOP Recent Request Table Activity')
ax.tick_params(axis='x', rotation=30)
ax.legend(fontsize=9)
ax.grid(axis='y', alpha=0.3)

ax = axes[2]
ax.bar(bop_df['wl_short'], bop_df['bop_phase_on'], color='#55A868', label='Phase ON')
ax.bar(bop_df['wl_short'], bop_df['bop_phase_off'], bottom=bop_df['bop_phase_on'].values, color='#C44E52', label='Phase OFF')
ax.set_ylabel('Count')
ax.set_title('BOP Phase Transitions (ON / OFF)')
ax.tick_params(axis='x', rotation=30)
ax.legend(fontsize=9)
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig5_bop_internals.png')
plt.close()

# ── Summary table ──
summary_rows = []
for wl in workloads:
    wl_short = wl.split('.')[1].replace('_r', '')
    for cfg in configs:
        row = df[(df['workload'] == wl) & (df['config'] == cfg)]
        if len(row) == 0:
            continue
        r = row.iloc[0]
        summary_rows.append({
            'Workload': wl_short,
            'Config': config_labels[cfg],
            'IPC': f"{r['ipc']:.4f}",
            'Speedup': f"{r['speedup_vs_no_pref']:.4f}",
            'L1 Miss Rate': f"{r['dcache_miss_rate']:.4f}",
            'L2 Miss Rate': f"{r['mlc_miss_rate']:.4f}",
            'Pref Accuracy': f"{r['pref_accuracy']:.4f}" if r['pref_accuracy'] > 0 else '-',
            'Pref Coverage': f"{r['pref_coverage_vs_no_pref']:.4f}" if r['pref_coverage_vs_no_pref'] > 0 else '-',
        })

summary_df = pd.DataFrame(summary_rows)
summary_df.to_csv(f'{OUTPUT_DIR}/summary_table.csv', index=False)

# ── Geometric mean speedup ──
print("\n=== Geometric Mean Speedup ===")
for cfg in ['stream', 'stride', 'bop']:
    speedups = []
    for wl in workloads:
        row = df[(df['workload'] == wl) & (df['config'] == cfg)]
        if len(row):
            speedups.append(row['speedup_vs_no_pref'].values[0])
    gmean = np.exp(np.mean(np.log(speedups)))
    print(f"  {config_labels[cfg]:>10s}: {gmean:.4f}")

print("\nAll figures saved.")
