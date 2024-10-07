import os
import matplotlib.pyplot as plt
import numpy as np

# Check available styles
available_styles = plt.style.available
if 'seaborn-darkgrid' in available_styles:
    plt.style.use('seaborn-darkgrid')
else:
    plt.style.use('default')

# Define the workloads in the results directory
workloads = [
    "cassandra_results", "clang_results", "drupal_results", "finagle-chirper_results",
    "finagle-http_results", "kafka_results", "mediawiki_results", "mysql_results",
    "postgres_results", "python_results", "tomcat_results", "verilator_results", "wordpress_results"
]

def extract_icache_stats(file_path):
    icache_miss = 0
    codverch_icache_miss = 0

    if not os.path.exists(file_path):
        print(f"{file_path} not found.")
        return icache_miss, codverch_icache_miss

    with open(file_path, 'r') as f:
        for line in f:
            columns = line.split()

            if len(columns) < 2:
                continue

            if columns[0] == "ICACHE_MISS":
                icache_miss = int(columns[1])
            elif columns[0] == "CODVERCH_ICACHE_ALU_JUMP_MISS":
                codverch_icache_miss = int(columns[1])

    return icache_miss, codverch_icache_miss

# Extract stats for each workload
icache_misses = []
codverch_icache_misses = []
for workload in workloads:
    file_path = os.path.join("results", workload, "memory.stat.0.out")
    icache_miss, codverch_icache_miss = extract_icache_stats(file_path)
    icache_misses.append(icache_miss)
    codverch_icache_misses.append(codverch_icache_miss)

# Calculate percentages
total_misses = [icache + codverch for icache, codverch in zip(icache_misses, codverch_icache_misses)]
icache_miss_percentages = [(icache / total) * 100 if total > 0 else 0 for icache, total in zip(icache_misses, total_misses)]
codverch_icache_miss_percentages = [(codverch / total) * 100 if total > 0 else 0 for codverch, total in zip(codverch_icache_misses, total_misses)]

# Calculate MPKI
total_instructions = 100_000_000  # 100 million
icache_miss_mpki = [(icache / total_instructions) * 1000 for icache in icache_misses]
codverch_icache_miss_mpki = [(codverch / total_instructions) * 1000 for codverch in codverch_icache_misses]

# Remove "_results" from workload names for x-axis labels
workload_labels = [workload.replace("_results", "") for workload in workloads]

# Define colors
colors = ['black', 'yellow']

# Plot the percentage data
bar_width = 0.35
x = np.arange(len(workload_labels))  # The label locations

plt.figure(figsize=(14, 7))
bars1 = plt.bar(x - bar_width/2, icache_miss_percentages, width=bar_width, label='Total I-Cache Miss %', color=colors[0])
bars2 = plt.bar(x + bar_width/2, codverch_icache_miss_percentages, width=bar_width, label='Consecutive ALU JUMP I-Cache Miss %', color=colors[1])

plt.xlabel('Workloads', fontsize=12, fontweight='bold')
plt.ylabel('Percentage of Cache Misses', fontsize=12, fontweight='bold')
plt.xticks(x, workload_labels, rotation=45, ha='right', fontsize=10)

# Adjust legend to have square boxes
plt.legend(loc='upper center', bbox_to_anchor=(0.5, 1.12), ncol=2, frameon=True, edgecolor='black', fancybox=False, handlelength=1, handleheight=1)

# Adjust layout to make space between the title and x-axis label
plt.tight_layout(rect=[0, 0, 1, 0.95], pad=2.5)

# Add values on top of each bar
for bar in bars1:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width()/2.0, yval + 0.5, f'{yval:.2f}%', ha='center', va='bottom', fontsize=8)
for bar in bars2:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width()/2.0 + 0.1, yval + 0.5, f'{yval:.2f}%', ha='center', va='bottom', fontsize=8)

plt.figtext(0.5, 0.01, 'Percentage of I-Cache Misses Induced by Consecutive <ALU, Control Flow> Instructions', ha='center', fontsize=14, fontweight='bold')
plt.savefig('cache_miss_percentages.png', dpi=300)
plt.show()

# Plot the MPKI data
plt.figure(figsize=(14, 7))
bars1 = plt.bar(x - bar_width/2, icache_miss_mpki, width=bar_width, label='Total I-Cache Miss MPKI', color=colors[0])
bars2 = plt.bar(x + bar_width/2, codverch_icache_miss_mpki, width=bar_width, label='Consecutive ALU JUMP I-Cache Miss MPKI', color=colors[1])

plt.xlabel('Workloads', fontsize=12, fontweight='bold')
plt.ylabel('MPKI (Misses Per Kilo Instructions)', fontsize=12, fontweight='bold')
plt.xticks(x, workload_labels, rotation=45, ha='right', fontsize=10)

# Adjust legend to have square boxes
plt.legend(loc='upper center', bbox_to_anchor=(0.5, 1.12), ncol=2, frameon=True, edgecolor='black', fancybox=False, handlelength=1, handleheight=1)

# Adjust layout to make space between the title and x-axis label
plt.tight_layout(rect=[0, 0, 1, 0.95], pad=2.5)

# Add values on top of each bar
for bar in bars1:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width()/2.0, yval + 0.05, f'{yval:.2f}', ha='center', va='bottom', fontsize=8)
for bar in bars2:
    yval = bar.get_height()
    plt.text(bar.get_x() + bar.get_width()/2.0, yval + 0.05, f'{yval:.2f}', ha='center', va='bottom', fontsize=8)

plt.figtext(0.5, 0.01, 'MPKI of I-Cache Misses Induced by Consecutive <ALU, Control Flow> Instructions', ha='center', fontsize=14, fontweight='bold')
plt.savefig('cache_miss_mpki.png', dpi=300)
plt.show()
