#!/bin/bash

# Set the base directories
address_space_dir="/users/deepmish/address-space"
results_dir="/users/deepmish/scarab/src/results"

# Create the results directory if it doesn't exist
mkdir -p "$results_dir"

# Loop through each subdirectory in address-space
for workload_dir in "$address_space_dir"/*; do
    if [ -d "$workload_dir" ]; then
        # Extract the workload name from the directory name
        workload_name=$(basename "$workload_dir")
        
        # Find the pt_something file in the workload directory
        pt_file=$(find "$workload_dir" -name "pt_$workload_name")
        
        if [ -f "$pt_file" ]; then
            # Create a subdirectory in results for each workload
            mkdir -p "$results_dir/$workload_name"
            
            # Run the command and capture the output in the results directory
            ./scarab --frontend pt --fetch_off_path_ops 0 --cbp_trace_r0="$pt_file" --inst_limit 100000000 > "$results_dir/$workload_name/output.txt"
            
            echo "Completed workload: $workload_name"
        else
            echo "No pt file found for workload: $workload_name"
        fi
    fi
done