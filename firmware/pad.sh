#!/bin/bash
dd if=$1 of=$2 bs=1 count=$(($(wc -c $1 | cut -d' ' -f2)-28))
dd if=$1 of="$2_end" bs=1 skip=$(($(wc -c $1 | cut -d' ' -f2)-28))
truncate -s 4194276 $2
cat "$2_end" >> $2
rm "$2_end"


