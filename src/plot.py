import os
import re
import matplotlib.pyplot as plt
from collections import defaultdict

# Directory containing the workload text files
directory = "plot"

# Initialize dictionaries to accumulate CPU cycles for user and kernel space
user_space_data = defaultdict(list)  # List of values per tuple to compute the mean later
kernel_space_data = defaultdict(list)

# Regular expression to match lines with instruction tuples and cumulative CPU cycles
pattern = re.compile(r"inst tuple \((User|Kernel)\): <(.+?)>, cumulative CCs: ([\d.]+)%")

# Parse each file in the directory
for filename in os.listdir(directory):
    if filename.endswith(".txt"):
        with open(os.path.join(directory, filename), "r") as file:
            for line in file:
                match = pattern.search(line)
                if match:
                    space = match.group(1)  # User or Kernel
                    inst_tuple = match.group(2)
                    cumulative_ccs = float(match.group(3))
                    # Append the cycle percentage to the list for this tuple
                    if space == "User":
                        user_space_data[inst_tuple].append(cumulative_ccs)
                    elif space == "Kernel":
                        kernel_space_data[inst_tuple].append(cumulative_ccs)

# Compute the arithmetic mean for each tuple in user and kernel spaces, keeping only those with a mean >= 1%
user_space_mean = {k: sum(v) / len(v) for k, v in user_space_data.items() if (sum(v) / len(v)) >= 1.0}
kernel_space_mean = {k: sum(v) / len(v) for k, v in kernel_space_data.items() if (sum(v) / len(v)) >= 1.0}

# Plotting
fig, ax = plt.subplots(figsize=(12, 6))

# Convert dictionaries to lists for plotting
user_labels = list(user_space_mean.keys())
user_values = list(user_space_mean.values())
kernel_values = [kernel_space_mean.get(k, 0) for k in user_labels]  # Align kernel values with user labels

# Plot user-space mean data
ax.barh(user_labels, user_values, color='skyblue', label="User Space Mean")

# Plot kernel-space mean data, offset for better readability
ax.barh(user_labels, kernel_values, color='salmon', label="Kernel Space Mean", left=user_values)

# Label and axis setup
ax.set_xlabel("Mean Cumulative CPU Cycles (%)")
ax.set_ylabel("Instruction Tuples")
ax.set_title("Mean Cumulative CPU Cycles for Instruction Tuples (User vs. Kernel Space Across Workloads)")
ax.legend()
plt.tight_layout()
plt.savefig("plot.png")

# Show the plot
plt.show()
