import os
import re
import matplotlib.pyplot as plt
import numpy as np
import matplotlib.font_manager

# Check available fonts on the system
available_fonts = matplotlib.font_manager.findSystemFonts(fontpaths=None, fontext='ttf')
print("Available fonts on your system:")
for font in available_fonts:
    print(font)

print("Starting to process application directories...")

# Set the root directory containing the subdirectories for datacenter applications
root_dir = "/users/deepmish/scarab/src/results"
if not os.path.exists(root_dir):
    raise ValueError(f"Root directory does not exist: {root_dir}")

# Initialize an empty dictionary to store data
data = {}

# Loop through each subdirectory
for app_dir in os.listdir(root_dir):
    app_path = os.path.join(root_dir, app_dir)
    if os.path.isdir(app_path):
        output_file = os.path.join(app_path, "output.txt")
        print(f"Looking for output file in: {app_path}")
        if os.path.exists(output_file):
            print(f"Found: {output_file}")
            with open(output_file, "r") as file:
                content = file.read()
            
            # Locate the "Final percentages" section
            match = re.search(r"Final percentages:\n(.*?)(?:\n\n|$)", content, re.DOTALL)
            if match:
                final_percentages = match.group(1)
                # Stop at "Computation" line if it exists
                final_percentages = re.split(r"\nComputation", final_percentages)[0]
                print(f"'Final percentages' section in {output_file}:\n{final_percentages}")
                
                # Parse the final percentages and populate the data dictionary
                percentages = {}
                for line in final_percentages.split('\n'):
                    if line.strip():
                        key, value = line.split(':')
                        percentages[key.strip()] = float(value.strip().replace('%', ''))
                
                data[app_dir] = percentages
            else:
                print(f"No 'Final percentages' section found in {output_file}")
                break
        else:
            print(f"Missing: {output_file}")
    else:
        print(f"Skipping non-directory: {app_path}")

# Check if data is populated
if not data:
    raise ValueError("No data found. Please check the output files or parsing logic.")

# Extract categories (keys) from the first application's data
categories = list(next(iter(data.values())).keys())

# Create a figure and axis with larger size for better readability
plt.rcParams['figure.dpi'] = 300
fig, ax = plt.subplots(figsize=(14, 8))

# Define the bar width
bar_width = 0.6

# Define application names and bar positions
applications = list(data.keys())
positions = np.arange(len(applications))

# Initialize the bottom positions for stacking bars
bottoms = np.zeros(len(applications))

# Define the new color palette
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

# Plot the stacked bars
for i, category in enumerate(categories):
    values = [data[app][category] for app in applications]
    bars = ax.bar(
        positions,
        values,
        bar_width,
        bottom=bottoms,
        label=category
    )
    bottoms += values
    
    # Add thin black border to bars
    for bar in bars:
        bar.set_edgecolor('black')
        bar.set_linewidth(0.5)

# Fallback to 'DejaVu Serif' if 'Times New Roman' is not available
font_family = 'Times New Roman' if 'Times New Roman' in available_fonts else 'DejaVu Serif'

# Change font to the selected font
plt.rcParams['font.family'] = font_family

# Set x-axis labels
ax.set_xticks(positions)
ax.set_xticklabels(applications, rotation=45, ha="right", fontsize=17, color='black')

# Set labels for axes


# Set labels for axes with font weight for better visibility
ax.set_ylabel("% of CPU Cycles Consumed", fontsize=17, color='#000000', fontweight='bold')
ax.set_xlabel("Datacenter Applications", fontsize=17, color='#000000', fontweight='bold')


# Create more detailed legend labels using arrows and line breaks
category_labels = {
    'cc_prev_mem_mem_curr_mem_mem': 'Instr. 1: MEM→MEM\nInstr. 2: MEM→MEM',
    'cc_prev_mem_mem_curr_reg_reg': 'Instr. 1: MEM→MEM\nInstr. 2: REG→REG',
    'cc_prev_mem_mem_curr_reg_mem': 'Instr. 1: MEM→MEM\nInstr. 2: REG↔MEM',
    
    'cc_prev_reg_reg_curr_reg_reg': 'Instr. 1: REG→REG\nInstr. 2: REG→REG',
    'cc_prev_reg_reg_curr_mem_mem': 'Instr. 1: REG→REG\nInstr. 2: MEM→MEM',
    'cc_prev_reg_reg_curr_reg_mem': 'Instr. 1: REG→REG\nInstr. 2: REG↔MEM',
    
    'cc_prev_reg_mem_curr_mem_mem': 'Instr. 1: REG↔MEM\nInstr. 2: MEM→MEM',
    'cc_prev_reg_mem_curr_reg_reg': 'Instr. 1: REG↔MEM\nInstr. 2: REG→REG',
    'cc_prev_reg_mem_curr_reg_mem': 'Instr. 1: REG↔MEM\nInstr. 2: REG↔MEM'
}

# Update legend labels
legend_labels = [category_labels.get(category, category) for category in categories]

# Customize the plot aesthetics
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.spines['left'].set_color('#333333')
ax.spines['bottom'].set_color('#333333')
ax.yaxis.label.set_color('#333333')
ax.xaxis.label.set_color('#333333')
ax.tick_params(axis='x', colors='black')
ax.tick_params(axis='y', colors='black')
ax.yaxis.grid(True, color='#EAEAEA')
ax.xaxis.grid(False)

# Set the background color
fig.patch.set_facecolor('#FFFFFF')
ax.set_facecolor('#FFFFFF')



# Create the legend with updated styling and more space for the two-line labels
legend = ax.legend(legend_labels, 
                  bbox_to_anchor=(0.5, 1.0),  # Position the legend just above the plot
                  loc='lower center',          # Place it at the center bottom of the axes
                  ncol=3,                      # Set the number of columns to 3
                  fontsize=13,
                  frameon=False,
                  title_fontsize=12,
                  columnspacing=2,           # Increase spacing between columns
                  handletextpad=2,
                  borderaxespad=2)         # Add space between legend title and legend

# Style the legend
legend.get_title().set_fontweight('bold')
for text in legend.get_texts():
    text.set_color('black')



# Adjust layout for better spacing
plt.tight_layout()

# Save the plot as a high-resolution PNG file with a white background
output_plot = os.path.join(root_dir, "cpu_cycles_mov_mov.png")
plt.savefig(output_plot, dpi=300, bbox_inches='tight', facecolor=fig.get_facecolor())
print(f"Plot saved as {output_plot}")

# Show the plot
plt.show()
