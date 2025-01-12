import os
import re
import matplotlib.pyplot as plt
import numpy as np

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
                # just break out of the loop
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

# Create a figure and axis
fig, ax = plt.subplots(figsize=(12, 6))

# Define the bar width
bar_width = 0.8

# Define application names and bar positions
applications = list(data.keys())
positions = np.arange(len(applications))

# Initialize the bottom positions for stacking bars
bottoms = np.zeros(len(applications))

# Define colors for each category (ensure enough unique colors)
colors = [
    "#2E8B57", "#ADFF2F", "#FFD700", "#FF4500", "#8A2BE2", 
    "#6495ED", "#DC143C", "#7FFF00", "#FF69B4", "#20B2AA"
]

# Plot the stacked bars
for i, category in enumerate(categories):
    values = [data[app][category] for app in applications]
    ax.bar(
        positions,
        values,
        bar_width,
        bottom=bottoms,
        label=category,
        color=colors[i % len(colors)]
    )
    bottoms += values

# Set x-axis labels
ax.set_xticks(positions)
ax.set_xticklabels(applications, rotation=45, ha="right", fontsize=10)

# Set labels and title
ax.set_ylabel("% of CPU Cycles Consumed", fontsize=12)
ax.set_xlabel("Datacenter Applications", fontsize=12)
ax.set_title("% Cycles in Reorder Buffer (ROB) by Instruction Tuple Type", fontsize=14)

# Add a legend
ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=10)

# Adjust layout for better spacing
plt.tight_layout()

# Save the plot as a PNG file
output_plot = os.path.join(root_dir, "datacenter_cycles_stacked_bar.png")
plt.savefig(output_plot, dpi=300)
print(f"Plot saved as {output_plot}")

# Show the plot
plt.show()

# Customize the plot aesthetics
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.spines['left'].set_color('#333333')
ax.spines['bottom'].set_color('#333333')
ax.yaxis.label.set_color('#333333')
ax.xaxis.label.set_color('#333333')
ax.tick_params(axis='x', colors='#333333')
ax.tick_params(axis='y', colors='#333333')
ax.yaxis.grid(True, color='#EAEAEA')
ax.xaxis.grid(False)

# Set the background color
fig.patch.set_facecolor('#FFFFFF')
ax.set_facecolor('#FFFFFF')

# Set the title color
ax.title.set_color('#333333')

# Adjust legend aesthetics
legend = ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=10, frameon=False)
for text in legend.get_texts():
    text.set_color('#333333')

# Save the plot as a PNG file with a white background
plt.savefig(output_plot, dpi=300, bbox_inches='tight', facecolor=fig.get_facecolor())
print(f"Plot saved as {output_plot}")

# Show the plot
plt.show()
