import os
import re
import matplotlib.pyplot as plt
from matplotlib import rcParams
from collections import defaultdict

# Set global font style to serif
rcParams['font.family'] = 'serif'

# Path to the result folder
result_folder = "/users/deepmish/scarab/src/result"

# Function to parse text files
def parse_files(folder_path):
    user_kernel_data = defaultdict(lambda: {"User": 0.0, "Kernel": 0.0})
    for filename in os.listdir(folder_path):
        if filename.endswith(".txt"):
            file_path = os.path.join(folder_path, filename)
            workload_name = filename.split(".")[0]
            with open(file_path, 'r') as f:
                for line in f:
                    # Extract cycle data for user/kernel space
                    user_match = re.search(r"inst tuple \(User\): .+?, cumulative CCs: ([\d\.]+)%", line)
                    kernel_match = re.search(r"inst tuple \(Kernel\): .+?, cumulative CCs: ([\d\.]+)%", line)
                    if user_match:
                        user_kernel_data[workload_name]["User"] += float(user_match.group(1))
                    if kernel_match:
                        user_kernel_data[workload_name]["Kernel"] += float(kernel_match.group(1))
    return user_kernel_data

# Function to plot stacked bar graph
def plot_stacked_bar(user_kernel_data):
    workloads = list(user_kernel_data.keys())
    user_values = [user_kernel_data[workload]["User"] for workload in workloads]
    kernel_values = [user_kernel_data[workload]["Kernel"] for workload in workloads]

    # Create figure with adjusted height to accommodate legend
    plt.figure(figsize=(12, 8.5))

    # Plotting
    bar_width = 0.4  # Reduced bar width
    
    # Stacked bars
    bars_user = plt.bar(workloads, user_values, label="User Mode", color="yellow", 
                       edgecolor="black", width=bar_width)
    bars_kernel = plt.bar(workloads, kernel_values, bottom=user_values, label="Kernel Mode", 
                         color="black", edgecolor="black", width=bar_width)

    # Aesthetic adjustments
    plt.title("Distribution of Instruction Tuples: User vs Kernel Space", fontsize=16, pad=50)
    plt.ylabel("CPU Cycles Consumed by Instruction Tuples (%)", fontsize=12)
    plt.xlabel("Datacenter Applications", fontsize=12)
    plt.xticks(rotation=45, ha="right", fontsize=10)

    # Add the legend below the title

    plt.legend(loc='upper center', bbox_to_anchor=(0.5, 1.09), fontsize=12, ncol=2, frameon=False)

    # Add a grid
    plt.grid(axis="y", linestyle="--", alpha=0.7)

    # Adjust layout to prevent label cutoff
    plt.tight_layout()
    
    # Save the plot
    plt.savefig("stacked_bar_user_kernel.png", bbox_inches='tight', dpi=300)
    plt.close()

# Main processing
user_kernel_data = parse_files(result_folder)
plot_stacked_bar(user_kernel_data)
print("Stacked bar graph has been generated and saved as 'stacked_bar_user_kernel.png'.")