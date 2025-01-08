#!/bin/bash

trace_dir="/users/deepmish/traces"
output_dir="/users/deepmish/scarab/src/result"

for trace in "$trace_dir"/*; do
    trace_name=$(basename "$trace")
    
    ./scarab --frontend pt --fetch_off_path_ops 0 --cbp_trace_r0="$trace" --inst_limit 100000000 > "$output_dir/$trace_name.txt"
done
