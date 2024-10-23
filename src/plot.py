import os
import pandas as pd
import re
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from matplotlib.colors import ListedColormap

# Directory containing the output files
output_dir = "/users/deepmish/scarab/src/output"

# Initialize a dictionary to store data
data = {}

# Iterate over each file in the output directory
for filename in os.listdir(output_dir):
    if filename.endswith("_output.txt"):
        app_name = filename.split('_')[1]  # Extract app name from filename
        file_path = os.path.join(output_dir, filename)

        with open(file_path, 'r') as file:
            for line in file:
                # Search for lines matching the pattern with instruction tuple and CPU cycle percentage
                match = re.search(r'inst tuple: <(.*)>, cumulative CCs: ([\d.]+)%', line)
                if match:
                    inst_tuple = match.group(1)  # Instruction tuple
                    cc_value = float(match.group(2))  # Cumulative CCs percentage

                    # Filter for tuples containing "CF" and percentage greater than 1%
                    if "CF" in inst_tuple and cc_value > 1:
                        # Initialize the instruction tuple in the data dictionary if not already present
                        if inst_tuple not in data:
                            data[inst_tuple] = {}
                        # Store the percentage value for the application
                        data[inst_tuple][app_name] = cc_value

# Convert the dictionary to a DataFrame
df = pd.DataFrame.from_dict(data, orient='index').fillna(0)

# Reset index to have instruction tuples as a column
df.reset_index(inplace=True)
df.rename(columns={'index': 'Instruction Tuple'}, inplace=True)

# Set the index to the Instruction Tuple for better plotting
df.set_index('Instruction Tuple', inplace=True)

# Create a custom colormap: pale yellow for 0, green shades for other values
cmap = ListedColormap(['#ffffcc', '#d9f0a3', '#addd8e', '#78c679', '#31a354', '#006837'])

# Define a normalization that treats zero values differently
norm = plt.Normalize(vmin=0, vmax=35)

# Plotting heatmap with the custom colormap
plt.figure(figsize=(24, 18))  # Increase figure size to make graph bigger
sns.heatmap(df, annot=True, fmt=".1f", cmap=cmap, linewidths=0.5, annot_kws={"size": 14}, cbar_kws={"shrink": 0.75},
            norm=norm, vmin=0, vmax=35)  # Use custom normalization
plt.title("% CPU Cycles Consumed by Consecutively Executed Tuples Containing Control Flow (CF) Instructions", fontsize=22)  # Bigger title font
plt.xlabel("Data Center Applications", fontsize=18)  # Larger x-axis label
plt.ylabel("Instruction Tuples", fontsize=18)  # Larger y-axis label
plt.xticks(rotation=60, ha="right", fontsize=16)  # Larger x-tick rotation and alignment
plt.yticks(fontsize=16)  # Larger y-tick labels
plt.tight_layout()

# Save the figure as a PNG file
plt.savefig("yellow.png", dpi=300)  # Save the figure
plt.show()