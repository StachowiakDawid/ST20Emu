#!/bin/bash
dd if=$1 of=$2 bs=1 count=$(($(wc -c $1 | cut -d' ' -f2)-28))
dd if=$1 of="$2_end" bs=1 skip=$(($(wc -c $1 | cut -d' ' -f2)-28))
dd if=/dev/zero of=zero_file bs=1 count=$3
truncate -s $((4194304-28-$3)) $2
cat $2 >> zero_file
cat "$2_end" >> zero_file
mv zero_file $2
rm "$2_end"


