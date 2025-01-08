import os
import re
import matplotlib.pyplot as plt
from collections import defaultdict

# Ensure Arial font
plt.rcParams["font.family"] = "Arial"

# Path to the directory containing text files
directory_path = "/users/deepmish/scarab/src/result/"

# Initialize dictionaries to store cycles by application and tuple type
data = defaultdict(lambda: defaultdict(int))
total_rs_cycles = 0  # Initialize total RS cycles

# Process each text file in the directory
for filename in os.listdir(directory_path):
    if filename.endswith(".txt"):
        file_path = os.path.join(directory_path, filename)
        with open(file_path, "r") as file:
            lines = file.readlines()
            current_application = os.path.splitext(filename)[0]  # Use filename as the application name
            current_tuple = None
            for line in lines:
                # Match table type (tuple name)
                table_match = re.match(r"Table name: (\w+)", line)
                if table_match:
                    current_tuple = table_match.group(1)
                # Match rs_cycles in "Debug" chunks
                if current_tuple:
                    rs_cycles_match = re.search(r"rs_cycles: (\d+)", line)
                    if rs_cycles_match:
                        rs_cycles = int(rs_cycles_match.group(1))
                        data[current_application][current_tuple] += rs_cycles
                        total_rs_cycles += rs_cycles

# Prepare data for plotting
applications = sorted(data.keys())
tuples = sorted({tuple_name for app in data.values() for tuple_name in app})

tuple_colors = [
    '#186158',  # dark green
    '#26e910',  # neon green
    '#444444',  # dark gray
    '#d3d3d3',  # light gray
    '#faf300',  # bright yellow
    '#fffacd',  # pastel yellow
    '#097991',  # dark teal
]

colors = tuple_colors[:len(tuples)]

# Calculate percentages for stacked bars
bars_data = {}
for t in tuples:
    bars_data[t] = []
    for app in applications:
        rs_cycles = data[app].get(t, 0)
        percentage = rs_cycles / total_rs_cycles * 100 if total_rs_cycles > 0 else 0
        # Debug statement
        print(f"Computing % for Tuple: {t}, Application: {app}, rs_cycles: {rs_cycles}, Total RS Cycles: {total_rs_cycles}, Percentage: {percentage:.2f}%")
        bars_data[t].append(percentage)

# Create figure with adjusted dimensions and spacing
plt.figure(figsize=(14, 9))  # Slightly taller to accommodate proper spacing
plt.subplots_adjust(top=0.80)  # Increase top margin for title and legend

# Create stacked bars
bottoms = [0] * len(applications)
for t, color in zip(tuples, colors):
    values = bars_data[t]
    plt.bar(applications, values, bottom=bottoms, label=t, color=color)
    bottoms = [sum(x) for x in zip(bottoms, values)]

# Customize the plot
plt.xlabel("Datacenter Applications", fontsize=12)
plt.ylabel("% Cycles in Reservation Station", fontsize=12)

# Add title with increased padding
plt.title("CPU Cycles Consumed by OP Tuples in Reservation Station", fontsize=16, pad=70)  # Reduced padding for title

# Move legend above title
plt.legend(
    fontsize=10,
    title_fontsize=12,
    loc='lower center',
    bbox_to_anchor=(0.5, 1.03),
    ncol=len(tuples),
    frameon=True,
    edgecolor='black',
    borderaxespad=0.5
)

# Adjust x and y ticks
plt.xticks(rotation=45, ha="right", fontsize=10)
plt.yticks(fontsize=10)

# Add gridlines
plt.grid(axis="y", linestyle="--", alpha=0.7)

# Save and display the plot with extra padding to prevent cutoff
output_path = "/users/deepmish/scarab/src/rs_cycles_op_tuples_top_legend.png"
plt.savefig(output_path, bbox_inches='tight', dpi=300, pad_inches=0.2)
plt.show()
