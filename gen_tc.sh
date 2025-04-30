#!/bin/bash

# Check if prefix argument is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <prefix>"
    exit 1
fi

prefix=$1

# Create 4 files with the given prefix
for i in {0..3}; do
    touch "./assignment3_traces/${prefix}_proc${i}.trace"
done

echo "Created 4 files with prefix '$prefix' in ./assignment3_traces:"
