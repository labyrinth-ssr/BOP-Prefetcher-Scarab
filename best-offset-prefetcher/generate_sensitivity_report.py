#!/usr/bin/env python3
"""BOP Parameter Sensitivity Analysis Report Generator"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib
matplotlib.rcParams['font.size'] = 11
matplotlib.rcParams['figure.dpi'] = 150

CSV_PATH = "bop-sensitivity.csv"
OUTPUT_DIR = "."

df = pd.read_csv(CSV_PATH)

workloads = sorted(df['workload'].unique())
wl_short = {w: w.split('.')[1].replace('_r', '') for w in workloads}

# Parameter groups
param_groups = {
    'RR Table Size': ['rr_64', 'rr_128', 'rr_512', 'rr_1024'],
    'Offset List': ['off_prime_64', 'off_all_32', 'off_all_64', 'off_prime_128', 'off_all_128', 'off_prime_512'],
    'Score Threshold': ['score_15', 'bop_default', 'score_63'],
    'Rounds per Phase': ['round_50', 'bop_default', 'round_200'],
    'BAD Score': ['bad_0', 'bad_2', 'bop_default', 'bad_4'],
}

param_labels = {
    'rr_64': '64', 'rr_128': '128', 'rr_512': '512 (def)', 'rr_1024': '1024',
    'off_prime_64': 'prime64', 'off_all_32': 'all32', 'off_all_64': 'all64',
    'off_prime_128': 'prime128', 'off_all_128': 'all128', 'off_prime_512': 'prime512',
    'score_15': '15', 'score_63': '63',
    'round_50': '50', 'round_200': '200',
    'bad_0': '0', 'bad_2': '2', 'bad_4': '4',
    'bop_default': 'default',
    'no_pref': 'no_pref',
}

colors_wl = plt.cm.Set2(np.linspace(0, 1, len(workloads)))

# ── Figure 1: Speedup vs bop_default heatmap ──
all_configs = [c for c in df['config'].unique() if c not in ['no_pref', 'bop_default']]
all_configs_sorted = ['rr_64', 'rr_128', 'rr_512', 'rr_1024',
                      'off_all_32', 'off_all_64', 'off_all_128',
                      'off_prime_64', 'off_prime_128', 'off_prime_512',
                      'round_50', 'round_200',
                      'score_15', 'score_63',
                      'bad_0', 'bad_2', 'bad_4']

heatmap_data = []
for wl in workloads:
    row = []
    for cfg in all_configs_sorted:
        r = df[(df['workload'] == wl) & (df['config'] == cfg)]
        if len(r):
            row.append(r['speedup_vs_bop_default'].values[0])
        else:
            row.append(1.0)
    heatmap_data.append(row)

fig, ax = plt.subplots(figsize=(16, 6))
hm = np.array(heatmap_data)
im = ax.imshow(hm, cmap='RdYlGn', aspect='auto', vmin=0.985, vmax=1.015)
ax.set_xticks(range(len(all_configs_sorted)))
ax.set_xticklabels([param_labels.get(c, c) for c in all_configs_sorted], rotation=45, ha='right')
ax.set_yticks(range(len(workloads)))
ax.set_yticklabels([wl_short[w] for w in workloads])
for i in range(len(workloads)):
    for j in range(len(all_configs_sorted)):
        val = hm[i, j]
        color = 'black' if 0.99 < val < 1.01 else 'white'
        ax.text(j, i, f'{val:.4f}', ha='center', va='center', fontsize=7, color=color)
ax.set_title('Speedup vs BOP Default Configuration')
cbar = plt.colorbar(im, ax=ax, shrink=0.8)
cbar.set_label('Speedup vs Default')
# Add vertical lines for group separators
ax.axvline(3.5, color='white', linewidth=2)
ax.axvline(9.5, color='white', linewidth=2)
ax.axvline(11.5, color='white', linewidth=2)
ax.axvline(13.5, color='white', linewidth=2)
ax.text(1.5, -0.8, 'RR Size', ha='center', fontsize=9, fontweight='bold')
ax.text(6.5, -0.8, 'Offset List', ha='center', fontsize=9, fontweight='bold')
ax.text(10.5, -0.8, 'Rounds', ha='center', fontsize=9, fontweight='bold')
ax.text(12.5, -0.8, 'Score', ha='center', fontsize=9, fontweight='bold')
ax.text(15.0, -0.8, 'BAD', ha='center', fontsize=9, fontweight='bold')
plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig_sens_heatmap.png', bbox_inches='tight')
plt.close()

# ── Figure 2: Per-parameter-group line plots (only sensitive workloads) ──
# Focus on workloads that show variation
sensitive_wls = ['520.omnetpp_r', '557.xz_r', '502.gcc_r']

fig, axes = plt.subplots(2, 3, figsize=(18, 10))

plot_groups = [
    ('RR Table Size', ['rr_64', 'rr_128', 'rr_512', 'rr_1024'], [64, 128, 512, 1024]),
    ('Rounds per Phase', ['round_50', 'bop_default', 'round_200'], [50, 100, 200]),
    ('Score Threshold', ['score_15', 'bop_default', 'score_63'], [15, 31, 63]),
]

for col, (group_name, configs, xvals) in enumerate(plot_groups):
    # Top row: speedup_vs_bop_default
    ax = axes[0][col]
    for i, wl in enumerate(workloads):
        speedups = []
        for cfg in configs:
            r = df[(df['workload'] == wl) & (df['config'] == cfg)]
            speedups.append(r['speedup_vs_bop_default'].values[0] if len(r) else 1.0)
        ax.plot(xvals, speedups, 'o-', label=wl_short[wl], color=colors_wl[i], markersize=5)
    ax.axhline(y=1.0, color='black', linestyle='--', alpha=0.3)
    ax.set_xlabel(group_name)
    ax.set_ylabel('Speedup vs Default')
    ax.set_title(f'{group_name} → Speedup')
    ax.legend(fontsize=7, ncol=2)
    ax.grid(alpha=0.3)
    if group_name == 'RR Table Size':
        ax.set_xscale('log', base=2)

    # Bottom row: prefetch accuracy
    ax = axes[1][col]
    for i, wl in enumerate(workloads):
        accs = []
        for cfg in configs:
            r = df[(df['workload'] == wl) & (df['config'] == cfg)]
            accs.append(r['pref_accuracy'].values[0] if len(r) else 0)
        ax.plot(xvals, accs, 'o-', label=wl_short[wl], color=colors_wl[i], markersize=5)
    ax.set_xlabel(group_name)
    ax.set_ylabel('Prefetch Accuracy')
    ax.set_title(f'{group_name} → Accuracy')
    ax.legend(fontsize=7, ncol=2)
    ax.grid(alpha=0.3)
    if group_name == 'RR Table Size':
        ax.set_xscale('log', base=2)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig_sens_params.png')
plt.close()

# ── Figure 3: Offset list comparison ──
off_configs = ['off_all_32', 'off_all_64', 'off_all_128', 'off_prime_64', 'off_prime_128', 'off_prime_512', 'bop_default']
off_labels = ['all32', 'all64', 'all128', 'prime64', 'prime128', 'prime512', 'default\n(paper)']

fig, axes = plt.subplots(1, 3, figsize=(18, 5))

# Speedup vs default
ax = axes[0]
x = np.arange(len(off_configs))
width = 0.12
for i, wl in enumerate(workloads):
    vals = []
    for cfg in off_configs:
        r = df[(df['workload'] == wl) & (df['config'] == cfg)]
        vals.append(r['speedup_vs_bop_default'].values[0] if len(r) else 1.0)
    ax.bar(x + i * width, vals, width, label=wl_short[wl], color=colors_wl[i])
ax.axhline(y=1.0, color='black', linestyle='--', alpha=0.5)
ax.set_xticks(x + 3 * width)
ax.set_xticklabels(off_labels, fontsize=8)
ax.set_ylabel('Speedup vs Default')
ax.set_title('Offset List → Speedup')
ax.legend(fontsize=7, ncol=2)
ax.grid(axis='y', alpha=0.3)

# Prefetch accuracy
ax = axes[1]
for i, wl in enumerate(workloads):
    vals = []
    for cfg in off_configs:
        r = df[(df['workload'] == wl) & (df['config'] == cfg)]
        vals.append(r['pref_accuracy'].values[0] if len(r) else 0)
    ax.bar(x + i * width, vals, width, label=wl_short[wl], color=colors_wl[i])
ax.set_xticks(x + 3 * width)
ax.set_xticklabels(off_labels, fontsize=8)
ax.set_ylabel('Prefetch Accuracy')
ax.set_title('Offset List → Accuracy')
ax.legend(fontsize=7, ncol=2)
ax.grid(axis='y', alpha=0.3)

# Prefetches issued
ax = axes[2]
for i, wl in enumerate(workloads):
    vals = []
    for cfg in off_configs:
        r = df[(df['workload'] == wl) & (df['config'] == cfg)]
        vals.append(r['bop_pref_issued'].values[0] if len(r) else 0)
    ax.bar(x + i * width, vals, width, label=wl_short[wl], color=colors_wl[i])
ax.set_xticks(x + 3 * width)
ax.set_xticklabels(off_labels, fontsize=8)
ax.set_ylabel('Prefetches Issued')
ax.set_title('Offset List → Issuance')
ax.legend(fontsize=7, ncol=2)
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig_sens_offset.png')
plt.close()

# ── Figure 4: BOP internal metrics sensitivity (omnetpp and xz - most sensitive) ──
fig, axes = plt.subplots(2, 3, figsize=(18, 10))
focus_wls = ['520.omnetpp_r', '557.xz_r']

for row, wl in enumerate(focus_wls):
    wl_data = df[df['workload'] == wl]

    # RR size sensitivity
    ax = axes[row][0]
    rr_cfgs = ['rr_64', 'rr_128', 'rr_512', 'rr_1024']
    rr_vals = [64, 128, 512, 1024]
    metrics = {
        'Speedup': [wl_data[wl_data['config'] == c]['speedup_vs_bop_default'].values[0] for c in rr_cfgs],
        'Pref Accuracy': [wl_data[wl_data['config'] == c]['pref_accuracy'].values[0] for c in rr_cfgs],
    }
    ax2 = ax.twinx()
    l1, = ax.plot(rr_vals, metrics['Speedup'], 'o-', color='#C44E52', label='Speedup')
    l2, = ax2.plot(rr_vals, metrics['Pref Accuracy'], 's--', color='#4C72B0', label='Accuracy')
    ax.set_xlabel('RR Table Size')
    ax.set_ylabel('Speedup vs Default', color='#C44E52')
    ax2.set_ylabel('Accuracy', color='#4C72B0')
    ax.set_title(f'{wl_short[wl]}: RR Table Size')
    ax.legend(handles=[l1, l2], fontsize=8)
    ax.set_xscale('log', base=2)
    ax.axhline(y=1.0, color='grey', linestyle='--', alpha=0.3)
    ax.grid(alpha=0.3)

    # Dropped page ratio
    ax = axes[row][1]
    drop_cfgs = ['rr_64', 'rr_128', 'rr_512', 'rr_1024']
    drops = [wl_data[wl_data['config'] == c]['bop_dropped_page_ratio'].values[0] * 100 for c in drop_cfgs]
    rr_hits = [wl_data[wl_data['config'] == c]['bop_rr_hit_score_rate'].values[0] * 100 for c in drop_cfgs]
    ax.bar(np.arange(4) - 0.15, drops, 0.3, label='Dropped Page %', color='#C44E52')
    ax.bar(np.arange(4) + 0.15, rr_hits, 0.3, label='RR Hit Score %', color='#55A868')
    ax.set_xticks(range(4))
    ax.set_xticklabels(['64', '128', '512', '1024'])
    ax.set_xlabel('RR Table Size')
    ax.set_ylabel('Rate (%)')
    ax.set_title(f'{wl_short[wl]}: Page Drop & RR Hit Rate')
    ax.legend(fontsize=8)
    ax.grid(axis='y', alpha=0.3)

    # Phase on ratio across all configs
    ax = axes[row][2]
    non_baseline = wl_data[~wl_data['config'].isin(['no_pref'])]
    configs_sorted = non_baseline.sort_values('speedup_vs_bop_default')
    ax.barh(range(len(configs_sorted)), configs_sorted['bop_phase_on_ratio'],
            color=['#55A868' if v >= 1.0 else '#C44E52' for v in configs_sorted['speedup_vs_bop_default']])
    ax.set_yticks(range(len(configs_sorted)))
    ax.set_yticklabels(configs_sorted['config'], fontsize=7)
    ax.set_xlabel('Phase ON Ratio')
    ax.set_title(f'{wl_short[wl]}: Phase ON Ratio by Config')
    ax.grid(axis='x', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/fig_sens_internals.png')
plt.close()

# ── Print summary statistics ──
print("=== Sensitivity Summary ===\n")
print("Configs that differ from bop_default (speedup != 1.0):\n")
diff_df = df[(df['config'] != 'no_pref') & (df['config'] != 'bop_default')]
diff_df = diff_df[diff_df['speedup_vs_bop_default'] != 1.0]
for _, row in diff_df.iterrows():
    print(f"  {wl_short[row['workload']]:>12s} | {row['config']:>16s} | speedup={row['speedup_vs_bop_default']:.6f} | "
          f"acc={row['pref_accuracy']:.4f} | issued={int(row['bop_pref_issued'])}")

print("\n=== Geomean Speedup vs Default by Config ===")
for cfg in all_configs_sorted:
    speedups = []
    for wl in workloads:
        r = df[(df['workload'] == wl) & (df['config'] == cfg)]
        if len(r):
            speedups.append(r['speedup_vs_bop_default'].values[0])
    if speedups:
        gmean = np.exp(np.mean(np.log(speedups)))
        print(f"  {cfg:>16s}: {gmean:.6f}")

print("\nAll sensitivity figures saved.")
