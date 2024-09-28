#!/bin/bash

workloads=("cassandra" "clang" "drupal" "finagle-chirper" "finagle-http" "kafka" 
           "mediawiki" "mysql" "postgres" "python" "tomcat" "verilator" "wordpress")

trace_base_path="/users/deepmish/datacenterGz"

for workload in "${workloads[@]}"; do
    echo "Processing workload: $workload"

    mkdir -p results/"${workload}_results"

    trace_path="$trace_base_path/$workload/trace"

    ./scarab --frontend pt --inst_limit 100000000 --cbp_trace_r0="$trace_path"

    files_to_copy=(
        "bp.stat.0.out"
        "per_branch_stats.csv"
        "stream.stat.0.csv.warmup"
        "stream.stat.0.out"
        "exec_stage.h"
        "ramulator.stat.out"
        "fetch.stat.0.csv"
        "inst.stat.0.csv"
        "power.stat.0.csv"
        "fetch.stat.0.out"
        "inst.stat.0.out"
        "memory.stat.0.csv"
        "power.stat.0.out"
        "memory.stat.0.out"
    )

    for file in "${files_to_copy[@]}"; do
        if [[ -f "$file" ]]; then
            cp "$file" results/"${workload}_results"/
        else
            echo "Warning: File $file not found for workload $workload."
        fi
    done

    echo "Finished processing workload: $workload"
    echo "-------------------------------------"
done

echo "All workloads processed."