import os
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.cm import ScalarMappable
from matplotlib.colors import Normalize

# Define the directory containing the text files
results_dir = "/users/deepmish/scarab/src/result"

# Initialize data structures
applications = []
instruction_tuples = set()
data = {}

# Parse the text files to gather data
for filename in os.listdir(results_dir):
    if filename.endswith(".txt"):
        app_name = filename.replace(".txt", "")
        applications.append(app_name)
        
        file_path = os.path.join(results_dir, filename)
        with open(file_path, "r") as file:
            lines = file.readlines()
            tuple_cycle_counts = {}
            total_cycles_rob = 0
            
            for line in lines:
                if line.startswith("Total ROB Cycles Consumed for"):
                    parts = line.split(":")
                    tuple_name = parts[0].split("for")[1].strip()
                    cycles = int(parts[1].strip())
                    tuple_cycle_counts[tuple_name] = cycles
                    instruction_tuples.add(tuple_name)
                if "Total cycles spent by all ops in ROB" in line:
                    total_cycles_rob = int(line.split(":")[1].strip())
            
            # Normalize cycle counts
            if total_cycles_rob > 0:
                data[app_name] = {
                    t: (tuple_cycle_counts.get(t, 0) / total_cycles_rob) * 100
                    for t in instruction_tuples
                }
            else:
                data[app_name] = {t: 0 for t in instruction_tuples}

# Ensure all instruction tuples appear in the same order
instruction_tuples = sorted(instruction_tuples)

# Create the stacked bar chart
x = np.arange(len(applications))
width = 0.6

fig, ax = plt.subplots(figsize=(14, 8))

# Generate gradient colors using a colormap
cmap = plt.cm.viridis  # You can change to other colormaps like 'plasma', 'cool', etc.
norm = Normalize(vmin=0, vmax=len(instruction_tuples) - 1)
colors = [cmap(norm(i)) for i in range(len(instruction_tuples))]

# Stack bars
bottoms = np.zeros(len(applications))
for i, t in enumerate(instruction_tuples):
    heights = [data[app].get(t, 0) for app in applications]
    ax.bar(x, heights, width, label=t, bottom=bottoms, color=colors[i])
    bottoms += heights

# Add labels and formatting
ax.set_xlabel("Data Center Applications", fontsize=14)
ax.set_ylabel("Percentage of Total ROB Cycles", fontsize=14)
ax.set_title("Stacked Bar Chart of ROB Cycles Consumed by Instruction Tuples (Gradient Colors)", fontsize=16)
ax.set_xticks(x)
ax.set_xticklabels(applications, rotation=45, ha="right", fontsize=12)
ax.legend(title="Instruction Tuples", fontsize=10, loc="upper left", bbox_to_anchor=(1, 1))

# Add percentage labels for each bar segment
for i, app in enumerate(applications):
    cumulative_height = 0
    for t in instruction_tuples:
        height = data[app].get(t, 0)
        if height > 0:
            ax.text(x[i], cumulative_height + height / 2, f"{height:.1f}%", 
                    ha="center", va="center", fontsize=8)
        cumulative_height += height

# Set aesthetics
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
ax.grid(axis="y", linestyle="--", alpha=0.7)
fig.tight_layout()

# Save and show the plot
plt.savefig("stacked_bar_gradient_plot.png", dpi=300, bbox_inches="tight")
plt.show()
