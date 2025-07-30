#!/bin/bash

output="merged_output.ts"
> "$output"  # Clear or create output file

offset=0
step=940000
end=30080000

while [[ $offset -le $end ]]; do
    chunk="chunk_${offset}.bin"
    if [[ -f "$chunk" ]]; then
        cat "$chunk" >> "$output"
    else
        echo "Missing: $chunk" >&2
    fi
    offset=$((offset + step))
done

echo "Merged into $output"
