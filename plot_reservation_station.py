import os
import re
import matplotlib.pyplot as plt
from collections import defaultdict
import matplotlib as mpl

# Set the font to match the academic style
plt.rcParams['font.family'] = 'serif'
plt.rcParams['font.serif'] = ['Times New Roman']
plt.rcParams['pdf.fonttype'] = 42
plt.rcParams['ps.fonttype'] = 42

# Function to process_file remains the same
def process_file(file_path):
    tuple_cycles = defaultdict(int)
    total_rs_cycles = 0
    breakdown_total_cycles = 0

    with open(file_path, 'r') as file:
        lines = file.readlines()

    current_tuple = None
    for line_idx, line in enumerate(lines):
        table_match = re.match(r"Table name: (\w+)", line)
        if table_match:
            current_tuple = table_match.group(1)
        
        debug_match = re.match(r"Debug: Key: \d+", line)
        if debug_match and current_tuple:
            instr1_cycle = instr2_cycle = rs_cycles = 0

            for i in range(1, 8):
                if len(lines) > line_idx + i:
                    instr1_match = re.match(r"\s+instr1_rs_issue_to_fu_cycle: (\d+)", lines[line_idx + i])
                    instr2_match = re.match(r"\s+instr2_rs_issue_to_fu_cycle: (\d+)", lines[line_idx + i])
                    rs_cycles_match = re.match(r"\s+rs_cycles: (\d+)", lines[line_idx + i])

                    if instr1_match:
                        instr1_cycle = int(instr1_match.group(1))
                    if instr2_match:
                        instr2_cycle = int(instr2_match.group(1))
                    if rs_cycles_match:
                        rs_cycles = int(rs_cycles_match.group(1))

            if instr1_cycle != 0 and instr2_cycle != 0:
                tuple_cycles[current_tuple] += rs_cycles

        breakdown_match = re.match(r"Breakdown of RS cycles \(Total cycles in RS: (\d+)\):", line)
        if breakdown_match:
            breakdown_total_cycles = int(breakdown_match.group(1))

    percentages = {
        tuple_type: (cycles / breakdown_total_cycles * 100) if breakdown_total_cycles > 0 else 0
        for tuple_type, cycles in tuple_cycles.items()
    }

    return tuple_cycles, percentages, breakdown_total_cycles

# Create figure with academic-style dimensions
fig = plt.figure(figsize=(8, 5))
ax = fig.add_subplot(111)

# Directory and data processing remains the same
input_directory = "/users/deepmish/scarab/src/meh"
all_tuple_cycles = defaultdict(list)
tuple_types = set()
file_names = []

for file_name in os.listdir(input_directory):
    file_path = os.path.join(input_directory, file_name)
    if os.path.isfile(file_path) and file_name.endswith('.txt'):
        tuple_cycles, percentages, total_cycles = process_file(file_path)

        for tuple_type, cycles in tuple_cycles.items():
            all_tuple_cycles[tuple_type].append(cycles)
            tuple_types.add(tuple_type)

        app_name = file_name.replace('.txt', '').title()
        file_names.append(app_name)

tuple_types = sorted(tuple_types)
cycle_data = [all_tuple_cycles[tuple_type] for tuple_type in tuple_types]

# Define a list of colors for the bars
colors = ['teal', 'orange', 'gold', 'skyblue', 'coral', 'limegreen', 'pink', 'purple', 'brown']

# Plot with academic styling
bottom_values = [0] * len(cycle_data[0])
for i, tuple_type in enumerate(tuple_types):
    plt.bar(
        range(len(cycle_data[0])),
        cycle_data[i],
        bottom=bottom_values,
        label=tuple_type,
        edgecolor='black',
        linewidth=1,
        color=colors[i % len(colors)]  # Assign color from the list
    )
    bottom_values = [sum(x) for x in zip(bottom_values, cycle_data[i])]

# Customize the plot
plt.grid(True, axis='y', linestyle='--', alpha=0.7)
plt.xlabel('Datacenter Applications', fontsize=10, fontweight='bold')
plt.ylabel('% CPU Cycles Spent in\nReservation Station', fontsize=10, fontweight='bold')

# Customize x-axis
plt.xticks(range(len(cycle_data[0])), file_names, rotation=45, ha='right', fontsize=9)

# Customize y-axis
plt.yticks(fontsize=9)

# Add legend with academic styling
legend = plt.legend(
    title="Tuple Types",
    bbox_to_anchor=(0.5, 1.15),
    loc='upper center',
    ncol=3,
    fontsize=9,
    frameon=True,
    edgecolor='black'
)
legend.get_title().set_fontsize(9)
legend.get_title().set_fontweight('bold')

# Tight layout to prevent label cutoff
plt.tight_layout()

# Add figure number and caption
plt.figtext(0.1, -0.1, 'Figure 1: Breakdown of CPU cycles spent in reservation station by tuple type.',
            fontsize=9, fontweight='bold')

# Save with high DPI for publication quality
output_path = os.path.join(input_directory, "reservation_station_tuple_cycles_stacked.png")
plt.savefig(output_path, dpi=300, bbox_inches='tight', pad_inches=0.5)
plt.close()

print(f"Figure saved as: {output_path}")
