import os
import matplotlib.pyplot as plt

# Define the results directory path
results_dir = "/users/deepmish/scarab/src/deepanjali/results"

# Define the workloads in the results directory
workloads = [
    "cassandra_results", "clang_results", "drupal_results", "finagle-chirper_results",
    "finagle-http_results", "kafka_results", "mediawiki_results", "mysql_results",
    "postgres_results", "python_results", "tomcat_results", "verilator_results", "wordpress_results"
]

# Function to extract the values from a specific file
def extract_icache_stats(file_path):
    icache_hit = 0
    icache_miss = 0
    codverch_icache_hit = 0

    with open(file_path, 'r') as f:
        for line in f:
            # Split the line by whitespace (handles varying spaces/tabs)
            columns = line.split()

            # Ensure the line is not empty and has at least two columns
            if len(columns) < 2:
                continue

            # Check if the first word in the line is the one we care about
            if columns[0] == "ICACHE_HIT":
                icache_hit = int(columns[1])  # extract the second element as hit value
            elif columns[0] == "ICACHE_MISS":
                icache_miss = int(columns[1])  # extract the second element as miss value
            elif columns[0] == "CODVERCH_ICACHE_HIT":
                codverch_icache_hit = int(columns[1])  # extract the second element as CODVERCH hit value

    return icache_hit, icache_miss, codverch_icache_hit

# Function to compute and return the percentages
def compute_percentages(icache_hit, icache_miss, codverch_icache_hit):
    total_accesses = icache_hit + icache_miss
    icache_hit_percentage = (icache_hit / total_accesses) * 100 if total_accesses > 0 else 0
    codverch_hit_percentage = (codverch_icache_hit / icache_hit) * 100 if icache_hit > 0 else 0

    return icache_hit_percentage, codverch_hit_percentage

# Prepare lists to store data for plotting
workload_names = []
icache_hit_percentages = []
codverch_hit_percentages = []
codverch_normalized_percentages = []

# Iterate through each workload directory
for workload in workloads:
    workload_dir = os.path.join(results_dir, workload)
    memory_file = os.path.join(workload_dir, "memory.stat.0.out")

    if os.path.exists(memory_file):
        icache_hit, icache_miss, codverch_icache_hit = extract_icache_stats(memory_file)
        icache_hit_percentage, codverch_hit_percentage = compute_percentages(icache_hit, icache_miss, codverch_icache_hit)

        # Normalize CODVERCH ICACHE Hit Percentage to ICACHE Hit Percentage
        codverch_normalized_percentage = (codverch_hit_percentage / icache_hit_percentage * 100) if icache_hit_percentage > 0 else 0

        # Append data for plotting
        workload_names.append(workload)
        icache_hit_percentages.append(icache_hit_percentage)
        codverch_hit_percentages.append(codverch_hit_percentage)
        codverch_normalized_percentages.append(codverch_normalized_percentage)

        # Print the results for each workload
        print(f"Workload: {workload}")
        print(f"ICACHE Hit Percentage: {icache_hit_percentage:.2f}%")
        print(f"CODVERCH ICACHE Hit Percentage: {codverch_hit_percentage:.2f}%")
        print(f"Normalized CODVERCH ICACHE Hit Percentage to ICACHE: {codverch_normalized_percentage:.2f}%")
        print("=" * 50)

    else:
        print(f"memory.stat.0.out not found for workload: {workload}")
        print("=" * 50)

# Plot the results
x = range(len(workload_names))  # Create an index for the x-axis

plt.figure(figsize=(10, 6))

# Plot ICACHE Hit Percentage
plt.bar(x, icache_hit_percentages, width=0.2, label='ICACHE Hit %', align='center')

# Plot CODVERCH ICACHE Hit Percentage
plt.bar([i + 0.2 for i in x], codverch_hit_percentages, width=0.2, label='CODVERCH ICACHE Hit %', align='center')

# Plot normalized CODVERCH ICACHE Hit Percentage
plt.bar([i + 0.4 for i in x], codverch_normalized_percentages, width=0.2, label='Normalized CODVERCH ICACHE Hit %', align='center')

# Add labels and legend
plt.xlabel('Workloads')
plt.ylabel('Percentage')
plt.title('ICACHE and CODVERCH ICACHE Hit Percentages by Workload')
plt.xticks([i + 0.2 for i in x], workload_names, rotation=90)
plt.legend()

# Display the plot
plt.tight_layout()
plt.savefig('icache_codverch_percentages_normalized.png')
plt.show()