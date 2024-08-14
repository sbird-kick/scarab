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

check_running_procs() {
    while (( $(jobs -r | wc -l) >= max_simul_proc )); do
        sleep 1
    done
}

# Default values
prev_type_nop=1
this_type_nop=1

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --nop_file)
            # This argument is ignored; we will create temp files dynamically
            shift 2
            ;;
        --starlab_write)
            starlab_write="$2"
            shift 2
            ;;
        --PREV_TYPE_NOP)
            prev_type_nop="$2"
            shift 2
            ;;
        --THIS_TYPE_NOP)
            this_type_nop="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

# Directory to read files from
directory="/simpoint_traces/"
pathToScarabDir="/home/humza/scarab/src/"
baseline_options=" --bp_mech tage64k --fdip_enable 1 --btb_entries 8192 --perfect_crs 1 --uop_cache_enable 0 --wp_collect_stats 1 --mem_req_buffer_entries 64 --ramulator_readq_entries 64 --fe_ftq_block_num 16 "
INST_LIMIT=30000000
WARMUP_INSTS=0
max_simul_proc=$(( $(nproc) - 1))

currdir=$(pwd)

prefix="breakdown"

# Create a temporary directory for storing temporary files
temp_dir=$(mktemp -d)

# Loop through all files in the directory and add them to the array
workloads=()
for file in "$directory"/*; do
    if [[ ! "$file" =~ \.(gz|tar|zip)$ ]]; then
        workloads+=("$(basename "$file")")
    fi
done

# Map NOP types to strings
prev_nop_type=$(map_nop_type $prev_type_nop)
this_nop_type=$(map_nop_type $this_type_nop)

# Create results directories
vanilla_results_dir="vanilla_${prev_nop_type}_${this_nop_type}"
fused_results_dir="fused_${prev_nop_type}_${this_nop_type}"
rm -rf $vanilla_results_dir $fused_results_dir
mkdir $vanilla_results_dir
mkdir $fused_results_dir

# First iteration with starlab_write 1
for workload in ${workloads[@]}; do
    trace_dir="$directory/$workload"
    trace_count=0
    
    for trace_file in "$trace_dir"/traces_simp/trace/*.zip; do
        trace_number=$(basename "$trace_file" .zip)
        RED='\033[0;31m'
        BLUE='\033[0;34m'
        NC='\033[0m' # No Color
        check_running_procs
        if [[ trace_count -eq 0 ]]; then
            if [[ $workload == pt_* ]]; then
                echo -ne "${RED}$workload${NC} is pt trace, processing ${BLUE}trace.gz${NC}                  \r"
            else
                echo -ne "${RED}$workload${NC} is a dynamorio trace, processing ${BLUE}$trace_number.zip${NC}                         \r"
            fi
        else
            echo -ne "${RED}$workload${NC} is a dynamorio trace, processing ${BLUE}$trace_count${NC} zips                        \r"
        fi

        # Create a unique temporary file for this run in the temp directory
        temp_nop_file="${temp_dir}/${workload}_${trace_number}_starlab_write_1.txt"

        # Change to scarab directory
        cd $pathToScarabDir

        if [[ $workload == pt_* ]]; then
            $pathToScarabDir/scarab --frontend pt --fetch_off_path_ops 0 $baseline_options --cbp_trace_r0="$directory/$workload/trace.gz" --full_warmup $WARMUP_INSTS --inst_limit $INST_LIMIT --nop_file $temp_nop_file --starlab_write 1 --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop > "$currdir/$vanilla_results_dir/${workload}_${trace_number}_starlab_write_1.txt" &
        else
            $pathToScarabDir/scarab --frontend memtrace --fetch_off_path_ops 0 $baseline_options --cbp_trace_r0="$trace_file" --memtrace_modules_log="$trace_dir/traces_simp/bin" --full_warmup $WARMUP_INSTS --inst_limit $INST_LIMIT --nop_file $temp_nop_file --starlab_write 1 --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop > "$currdir/$vanilla_results_dir/${workload}_${trace_number}_starlab_write_1.txt" &
        fi

        # Return to the original directory
        cd $currdir

        pids+=($!)
        trace_count=$((trace_count + 1))
    done
done

# Wait for all background processes to finish
for pid in ${pids[@]}; do
    wait $pid
done

# Now run with starlab_write 0 and use the temporary files created earlier
starlab_write=0
for workload in ${workloads[@]}; do
    trace_dir="$directory/$workload"
    trace_count=0
    
    for trace_file in "$trace_dir"/traces_simp/trace/*.zip; do
        trace_number=$(basename "$trace_file" .zip)
        RED='\033[0;31m'
        BLUE='\033[0;34m'
        NC='\033[0m' # No Color
        check_running_procs
        if [[ trace_count -eq 0 ]]; then
            if [[ $workload == pt_* ]]; then
                echo -ne "${RED}$workload${NC} is pt trace, processing ${BLUE}trace.gz${NC}                  \r"
            else
                echo -ne "${RED}$workload${NC} is a dynamorio trace, processing ${BLUE}$trace_number.zip${NC}                         \r"
            fi
        else
            echo -ne "${RED}$workload${NC} is a dynamorio trace, processing ${BLUE}$trace_count${NC} zips                        \r"
        fi

        # Reuse the temporary file from the previous run
        temp_nop_file="${temp_dir}/${workload}_${trace_number}_starlab_write_1.txt"

        # Change to scarab directory
        cd $pathToScarabDir

        if [[ $workload == pt_* ]]; then
            $pathToScarabDir/scarab --frontend pt --fetch_off_path_ops 0 $baseline_options --cbp_trace_r0="$directory/$workload/trace.gz" --full_warmup $WARMUP_INSTS --inst_limit $INST_LIMIT --nop_file $temp_nop_file --starlab_write 0 --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop > "$currdir/$fused_results_dir/${workload}_${trace_number}_starlab_write_0.txt" &
        else
            $pathToScarabDir/scarab --frontend memtrace --fetch_off_path_ops 0 $baseline_options --cbp_trace_r0="$trace_file" --memtrace_modules_log="$trace_dir/traces_simp/bin" --full_warmup $WARMUP_INSTS --inst_limit $INST_LIMIT --nop_file $temp_nop_file --starlab_write 0 --PREV_TYPE_NOP $prev_type_nop --THIS_TYPE_NOP $this_type_nop > "$currdir/$fused_results_dir/${workload}_${trace_number}_starlab_write_0.txt" &
        fi

        # Return to the original directory
        cd $currdir

        pids+=($!)
        trace_count=$((trace_count + 1))
    done
done

# Wait for all background processes to finish
for pid in ${pids[@]}; do
    wait $pid
done

# Clean up temporary files
rm -rf $temp_dir
