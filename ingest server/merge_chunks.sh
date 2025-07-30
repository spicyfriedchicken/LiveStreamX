#!/bin/bash

output="merged_output.ts"
> "$output"  # Clear or create output file

offsets=(
    0 940000 1880000 2820000 3760000 4700000 5640000 6580000 7520000 8460000
    9400000 10340000 11280000 12220000 13160000 14100000 15040000 15980000
    16920000 17860000 18800000 19740000 20680000 21620000
)

for offset in "${offsets[@]}"; do
    chunk="chunk_${offset}.ts"
    if [[ -f "$chunk" ]]; then
        cat "$chunk" >> "$output"
    else
        echo "Missing: $chunk" >&2
    fi
done

echo "Merged into $output"
