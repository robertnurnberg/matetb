#!/bin/bash

set -e

make clean && make -j
MULTIPV_FILE=matetb_multipv.epd
rm -f $MULTIPV_FILE

while IFS= read -r epd_line || [[ -n "$epd_line" ]]; do
    echo "  Line: '$epd_line'"
    if [[ "$epd_line" == *"needs engine"* ]]; then
        echo "    Skipping"
        continue
    fi    
    epd="${epd_line%%;*}"
    output=$( ./matetb_threaded --verbose 2 --epd "$epd" 2>&1 )
    echo "$output" | grep "^Child FEN:" | sed 's/Child FEN: //g' >>$MULTIPV_FILE
done < "new.epd"

echo "Results written to $MULTIPV_FILE."
