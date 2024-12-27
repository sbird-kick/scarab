#!/bin/bash


SCARAB_DIR="/users/deepmish/scarab/src"
DATACENTER_DIR="/users/deepmish/datacenterGz"
RESULTS_DIR="$SCARAB_DIR/result"

mkdir -p "$RESULTS_DIR"

cd "$SCARAB_DIR" || { echo "Failed to change directory to $SCARAB_DIR"; exit 1; }

# INST_LIMIT=100000000
INST_LIMIT=10

for WORKLOAD_DIR in "$DATACENTER_DIR"/*; do
  if [ -d "$WORKLOAD_DIR" ]; then
    WORKLOAD_NAME=$(basename "$WORKLOAD_DIR")
    TRACE_FILE="$WORKLOAD_DIR/trace"

    if [ -f "$TRACE_FILE" ]; then
      echo "Running workload: $WORKLOAD_NAME"

      # Run Scarab
      ./scarab --frontend pt --inst_limit $INST_LIMIT --cbp_trace_r0="$TRACE_FILE" > "$RESULTS_DIR/${WORKLOAD_NAME}.txt"

      if [ $? -eq 0 ]; then
        echo "Workload $WORKLOAD_NAME completed successfully. Results saved to ${RESULTS_DIR}/${WORKLOAD_NAME}.txt"
      else
        echo "Error running workload $WORKLOAD_NAME. Check output for details."
      fi
    else
      echo "Trace file not found for workload $WORKLOAD_NAME. Skipping..."
    fi
  fi
done