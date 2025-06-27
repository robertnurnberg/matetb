#!/bin/bash

set -e

make clean && make -j

while IFS= read -r epd_line || [[ -n "$epd_line" ]]; do
    echo "  Line: '$epd_line'"
    if [[ "$epd_line" == *"needs engine"* ]]; then
        echo "    Skipping"
        continue
    fi    
    epd="${epd_line%%;*}"
    output=$( ./matetb_threaded --epd "$epd" 2>&1 )
    matetrack_line=$(echo "$output" | grep -A1 "^Matetrack:" | tail -n 1 | sed 's/;.*//')
    echo "    Mate found: '$matetrack_line'"
    if [[ "$matetrack_line" == "$epd" ]]; then
        echo "    RESULT: MATCH"
    else
        if [[ "$epd_line" == *"finds"* ]]; then
            matetrack_line=$(echo "$matetrack_line" | sed 's/#.*//')
            epd=$(echo "$epd" | sed 's/#.*//')
            if [[ "$matetrack_line" == "$epd" ]]; then
                echo "    RESULT: EXPECTED PARTIAL MATCH"
                continue
            fi
        fi    
        echo "    RESULT: NO MATCH"
        exit 1
    fi
done < "matetb.epd"

echo "Check finished."
