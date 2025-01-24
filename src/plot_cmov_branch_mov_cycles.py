import matplotlib.pyplot as plt
import numpy as np

# Data for the chart
workloads = [
    "finagle-chirper", "mediawiki", "verilator", "finagle-http", 
    "python", "mysql", "clang", "tomcat", 
    "wordpress", "kafka", "cassandra"
]
cpu_cycles = [5.0, 4.7, 2.7, 5.8, 5.0, 2.0, 7.1, 4.5, 12.5, 3.5, 13.8]

# Dark teal color
color = "#004c4c"

# Create the plot
fig, ax = plt.subplots(figsize=(12, 6))

# Create vertical bars with darker teal and boxed edges
bars = ax.bar(workloads, cpu_cycles, color=color, edgecolor="black", linewidth=1, width=0.6)

# Add values on top of bars
for bar, value in zip(bars, cpu_cycles):
    ax.text(
        bar.get_x() + bar.get_width() / 2, value + 0.3, 
        f"{value:.1f}%", ha="center", va="bottom", fontsize=10, color="black"
    )

# Add horizontal and vertical black lines (axes)
ax.axhline(y=0, color="black", linewidth=1.2)
ax.axvline(x=-0.5, color="black", linewidth=1.2)  # Align with the first bar

# Aesthetic adjustments
# ax.set_title("% CPU cycles consumed by <Cond. Br, MOV>", fontsize=14, pad=15, loc="center")
ax.set_ylabel("% CPU cycles consumed by <Cond. Br, MOV>", fontsize=12, labelpad=10)
ax.set_ylim(0, 15)
ax.set_xticks(range(len(workloads)))
ax.set_xticklabels(workloads, fontsize=10, rotation=45, ha="right")
ax.yaxis.set_ticks_position("left")
ax.xaxis.set_ticks_position("none")

# Remove unnecessary spines for a clean look
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)
ax.spines["bottom"].set_visible(False)
ax.spines["left"].set_visible(False)

# Background color
ax.set_facecolor("white")
fig.patch.set_facecolor("white")

# Adjust layout and save the plot
plt.tight_layout()

# Display the plot
plt.show()

plt.savefig("cpu_cycles_cond_br_mov.png", dpi=300)