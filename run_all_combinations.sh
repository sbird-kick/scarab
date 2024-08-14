#!/bin/bash

# Function to map NOP type numbers to strings
map_nop_type() {
    case $1 in
        0) echo "NOP" ;;
        1) echo "MOV" ;;
        2) echo "CALL" ;;
        3) echo "PREFETCH" ;;
        4) echo "PUSH" ;;
        5) echo "POP" ;;
        6) echo "LOCK" ;;
        7) echo "ALU" ;;
        *) echo "UNKNOWN" ;;
    esac
}

# Path to the run_tuple.sh script
script_path="run_tuples.sh"

# Directory to store the results
results_dir="combined_results"
mkdir -p $results_dir

# Loop through all combinations of prev_type_nop and this_type_nop
for prev_type_nop in {0..7}; do
    for this_type_nop in {0..7}; do
        # Format the types as two digits
        # Get the corresponding OP names
        prev_nop_type=$(map_nop_type $prev_type_nop)
        this_nop_type=$(map_nop_type $this_type_nop)

        # Create a unique directory for each combination
        combination_dir="$results_dir/${prev_nop_type}_${this_nop_type}"
        mkdir -p $combination_dir

        # Run the script with the current combination of NOP types
        echo "Running script with --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop"
        bash $script_path --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop

        # Move the results to the combination directory
        mv vanilla_${prev_nop_type}_${this_nop_type} "$combination_dir"
        mv fused_${prev_nop_type}_${this_nop_type} "$combination_dir"
    done
done
