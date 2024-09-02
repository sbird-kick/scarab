#!/bin/bash

path_to_simple_parser=/users/DTRDNT/datacenter-efficiency/scarab-sim/decoder/a.out
path_to_scarab_classes=/users/DTRDNT/datacenter-efficiency/scarab-sim/decoder/scarab_classes.txt
path_to_trace_file=$(realpath ~/mysql)
path_to_scarab_dir=/users/DTRDNT/scarab/src/

workdir=$(pwd)

# rm -rf $path_to_scarab_classes

while true; do
    # Capture stdout in a variable
    output=$($path_to_simple_parser --trace-file "$path_to_trace_file"  --scarab-classes-file $path_to_scarab_classes $2>&1)

    # Check if the command succeeded
    if [ $? -eq 0 ]; then
        echo "DONE!"
        break
    else
        # Extract the address using grep
        address=$(echo "$output" | grep -oP 'UNMAPPED ADDRESS:\s*\K[0-9a-fA-F]+')
        xed_iclass=$(echo "$output" | grep -oP 'UNMAPPED ADDRESS:\s*[0-9a-fA-F]+\s*\K\S+')

        echo "Command failed, retrying..."
        echo "Last output:"
        echo "$output"

        if [ -n "$address" ]; then
            echo "Extracted Address: $address"
        else
            echo "No address found in the output."
            exit 1
        fi

        if [ -n "$xed_iclass" ]; then
            echo "Extracted xed_iclass: $xed_iclass"
        else
            echo "No xed_iclass found in the output."
        fi

        cd $path_to_scarab_dir
        
        scarab_output=$(./scarab --frontend pt --fetch_off_path_ops 0 --cbp_trace_r0=$path_to_trace_file --EXAMINATION_ICLASS_ADDRESS $address 2>&1)

        scarab_class=$(echo $scarab_output | grep -oP 'SCARAB TYPE:\s*\K\S+')

        echo $(echo $scarab_output | grep "scarab inst address!")

        echo "scarab class: $scarab_class"

        echo "$xed_iclass $scarab_class" >> $path_to_scarab_classes


        cd $workdir
    fi
done
