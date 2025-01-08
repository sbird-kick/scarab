import os
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import Normalize, LinearSegmentedColormap
import matplotlib.font_manager

# Check available fonts on the system
available_fonts = matplotlib.font_manager.findSystemFonts(fontpaths=None, fontext='ttf')
print("Available fonts on your system:")
for font in available_fonts:
    print(font)

# Define the directory containing the text files
results_dir = "/users/deepmish/reorder_buffer/scarab/src/result"

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
            
            # Read data from the file
            for line in lines:
                if line.startswith("Total ROB Cycles Consumed for"):
                    parts = line.split(":")
                    tuple_name = parts[0].split("for")[1].strip()
                    cycles = int(parts[1].strip())
                    tuple_cycle_counts[tuple_name] = cycles
                    instruction_tuples.add(tuple_name)
            
            # Compute total cycles spent by all tuples in ROB
            total_cycles_rob = sum(tuple_cycle_counts.values())
            print(f"Total cycles for {app_name}: {total_cycles_rob}")
            
            # Normalize cycle counts (percentage calculation)
            if total_cycles_rob > 0:
                data[app_name] = {}
                for t in instruction_tuples:
                    cycles = tuple_cycle_counts.get(t, 0)
                    percentage = (cycles / total_cycles_rob) * 100
                    print(f"Percentage for {t} in {app_name}: {cycles}/{total_cycles_rob} = {percentage:.2f}%")
                    data[app_name][t] = percentage
            else:
                print(f"No cycles recorded for {app_name}")
                data[app_name] = {t: 0 for t in instruction_tuples}

# Ensure all instruction tuples appear in the same order
instruction_tuples = sorted(instruction_tuples)

# Create the stacked bar chart
x = np.arange(len(applications))
width = 0.6

fig, ax = plt.subplots(figsize=(14, 8))

colors = [
    '#186158',  # dark green
    '#26e910',  # neon green
    '#444444',  # dark gray
    '#d3d3d3',  # light gray
    '#faf300',  # bright yellow
    '#fffacd',  # pastel yellow
    '#097991',  # dark teal
    '#8fdde7',  # light teal
    '#351c75',  # orange-red
]

# Set the colors directly
ax.set_prop_cycle('color', colors)

# Stack bars
bottoms = np.zeros(len(applications))
for i, t in enumerate(instruction_tuples):
    heights = [data[app].get(t, 0) for app in applications]
    bars = ax.bar(x, heights, width, label=t, bottom=bottoms)
    bottoms += heights
    
    # Add thin black border to bars
    for bar in bars:
        bar.set_edgecolor('black')
        bar.set_linewidth(0.5)

# Fallback to 'DejaVu Serif' if 'Times New Roman' is not available
font_family = 'Times New Roman' if 'Times New Roman' in available_fonts else 'DejaVu Serif'

# Change font to the selected font
plt.rcParams['font.family'] = font_family

# Add labels and formatting
ax.set_xlabel("Datacenter Applications", fontsize=14)
ax.set_ylabel("% Cycles in Reorder Buffer (ROB)", fontsize=14)
ax.set_title("ROB Cycles Consumed by OP Tuples", fontsize=16)
ax.set_xticks(x)
ax.set_xticklabels(applications, rotation=45, ha="right", fontsize=12)
ax.legend(title="Instruction Tuples", fontsize=10, loc="upper left", bbox_to_anchor=(1, 1))

# Set aesthetics
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
ax.grid(axis="y", linestyle="--", alpha=0.7)

# Adjust layout
fig.tight_layout()

# Save and show the plot
plt.savefig("stacked_bar_gradient_plot.png", dpi=300, bbox_inches="tight")
plt.show()
