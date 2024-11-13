#!/bin/bash

# Set the base directories
address_space_dir="/users/deepmish/datacenterGz"
results_dir="/users/deepmish/microarch/scarab/src/results"

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
            workload_result_dir="$results_dir/$workload_name"
            mkdir -p "$workload_result_dir"
            
            # Run the command and capture the output in the results directory
            ./scarab --frontend pt --fetch_off_path_ops 0 --cbp_trace_r0="$pt_file" --inst_limit 100000000 > "$workload_result_dir/output.txt"
            
            # Define the list of files to copy
            files_to_copy=(
                "bp.stat.0.csv" "bp.stat.0.out" "core.stat.0.csv" "core.stat.0.out"
                "fetch.stat.0.csv" "fetch.stat.0.out" "inst.stat.0.csv" "inst.stat.0.out"
                "l2l1pref.stat.0.csv" "l2l1pref.stat.0.out" "memory.stat.0.csv" "memory.stat.0.out"
                "power.stat.0.csv" "power.stat.0.out" "pref.stat.0.csv" "pref.stat.0.out"
            )

            # Copy each specified file to the results folder of the respective workload directory
            for file in "${files_to_copy[@]}"; do
                if [ -f "$file" ]; then
                    cp "$file" "$workload_result_dir/"
                else
                    echo "Warning: $file not found for workload: $workload_name"
                fi
            done
            
            echo "Completed workload: $workload_name"
        else
            echo "No pt file found for workload: $workload_name"
        fi
    fi
done