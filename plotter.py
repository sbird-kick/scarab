import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import matplotlib.colors as mcolors
import seaborn as sns

# Set font to serif
plt.rcParams['font.family'] = 'serif'

# Function to extract workload names and prioritize
def extract_workload(name):
    if name.startswith('pt_tanvir_'):
        return name[10:], 2
    elif name.startswith('pt_'):
        return name[3:], 3
    else:
        return name, 1

# Read the CSV file into a DataFrame
df = pd.read_csv('../squashed.csv', index_col=0)

# Extract workload names and prioritize
workloads = [extract_workload(name) + (name,) for name in df.index]

# Sort by workload and priority
workloads.sort(key=lambda x: (x[0], x[1]))

# Deduplicate by taking the first occurrence of each workload
unique_workloads = {}
for workload, priority, original_name in workloads:
    if workload not in unique_workloads:
        unique_workloads[workload] = original_name

# Create a new DataFrame with the prioritized and deduplicated index
df = df.loc[unique_workloads.values()]

df.index = df.index.str.replace('pt_tanvir_', '').str.replace('pt_', '')

# Replace negative percentage reductions with zero
df[df < 0] = 0

# Sum up the percentage reductions for each combination
df_sum = df.groupby(df.columns, axis=1).sum()

# Extract unique `inst1` and `inst2` values
inst1_values = sorted(set(col.split(',')[0] for col in df_sum.columns))
inst2_values = sorted(set(col.split(',')[1] for col in df_sum.columns))
num_inst1 = len(inst1_values)

# Generate base colors for each `inst1` using a distinct color palette
base_colors = sns.color_palette("husl", num_inst1)
base_color_map = {inst1: base_colors[i] for i, inst1 in enumerate(inst1_values)}

# Function to generate a color by varying hue and brightness based on percentage reduction
def adjust_color(base_color, variation, is_significant):
    # Convert base color to HSV
    base_hsv = mcolors.rgb_to_hsv(base_color[:3])

    # Adjust the hue by a small amount based on `inst2`
    hue_shift = 0.5 * (variation / (len(inst2_values) - 1))  # Scale variation to [0, 1]
    new_hsv = base_hsv.copy()
    new_hsv[0] = (new_hsv[0] + hue_shift) % 1.0  # Adjust hue

    # Adjust brightness based on significance
    brightness_shift = 0.5 if is_significant else -0.3
    new_hsv[2] = min(max(new_hsv[2] + brightness_shift, 0), 1)  # Adjust brightness

    # Convert back to RGB
    return mcolors.hsv_to_rgb(new_hsv)

# Calculate the average percentage reduction for each tuple combination
average_reductions = df.mean(axis=0)

# Sort tuple combinations by average percentage reduction and select top 10 manually
sorted_combinations = average_reductions.sort_values(ascending=False)
top_10_combinations = sorted_combinations.head(10).index

# Filter df_sum to include only the top 10 combinations
df_sum_top10 = df_sum[top_10_combinations]

# Assign colors to each `inst1_inst2` combination
plot_colors = []
for col in df_sum_top10.columns:
    inst1, inst2 = col.split(',')
    base_color = base_color_map[inst1]
    # Calculate variation based on `inst2` (index in inst2_values)
    variation = inst2_values.index(inst2)
    # Determine if the reduction is significant
    is_significant = (df_sum[col] > 5).any()  # Check if any workload has >5% reduction
    color = adjust_color(base_color, variation, is_significant)
    plot_colors.append(color)

# Plot the stacked bar graph with the assigned colors
ax = df_sum_top10.plot(kind='bar', stacked=True, figsize=(12, 8), color=plot_colors)

# Set plot labels and title
plt.xlabel('Workload')
plt.ylabel('IPC Percentage consumed by tuple')
plt.title('Percentage in CPU Cycles Consumed by Instruction Tuples')

# Adjust the legend to be inside the plot area
handles, labels = ax.get_legend_handles_labels()
sorted_handles_labels = sorted(zip(handles, labels), key=lambda x: average_reductions[x[1]], reverse=True)
handles, labels = zip(*sorted_handles_labels)
plt.legend(handles, labels, title='Tuple Combination', loc='best')

# Adjust layout to make room for the legend
plt.tight_layout()

# Save the plot
plt.savefig('percentage_consumed.png', bbox_inches='tight')

# Show the plot
plt.show()
