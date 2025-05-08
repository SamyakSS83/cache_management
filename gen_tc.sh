#!/bin/bash

# Check if prefix argument is provided
# if [ $# -ne 1 ]; then
#     echo "Usage: $0 <prefix>"
#     exit 1
# fi

# prefix=$1

# Create 4 files with the given prefix
for i in {1..9}; do
    ./L1simulate -t assignment3_traces/app"$i" -s 6 -E 2 -b 5 -o ./outputs/output_"$i".txt
done


echo "Created 9 files with outputs in ./outputs:"
