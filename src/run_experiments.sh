#!/bin/bash

# Define the full paths to the workloads
workloads=(
    "/users/deepmish/datacenterGz/tomcat/pt_tomcat"
    "/users/deepmish/datacenterGz/verilator/pt_verilator"
    "/users/deepmish/datacenterGz/wordpress/pt_wordpress"
    "/users/deepmish/datacenterGz/postgres/pt_postgres"
    "/users/deepmish/datacenterGz/python/pt_python"
)

# Create an output directory if it doesn't exist
output_dir="/users/deepmish/scarab/src/output"
mkdir -p "$output_dir"

# Loop through each workload and run the command
for workload in "${workloads[@]}"; do
    # Extract the name of the workload for the output file name
    workload_name=$(basename "$workload")

    # Construct the command and output file name
    command="./scarab --frontend pt --fetch_off_path_ops 0 --cbp_trace_r0=$workload --inst_limit 100000000"
    output_file="$output_dir/${workload_name}_output.txt"

    # Run the command and redirect output to the corresponding file
    echo "Running $workload_name..."
    $command > "$output_file"

    echo "Output saved to $output_file"
done

echo "All workloads have been executed."