#!/bin/sh
# erase-target.sh [GB-label] -- in Recovery: erase the blank install target (found by size as diskutil prints it, default "64.4 GB") as HFS+ "Macintosh".
L=${1:-64.4 GB}; D=$(diskutil list | grep -F "*$L" | awk '{print $NF}')
echo "target: $D"; [ -n "$D" ] && diskutil eraseDisk JHFS+ Macintosh GPT $D
