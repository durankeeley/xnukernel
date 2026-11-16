#!/bin/bash
# fetch-deps.sh -- clone the pinned third-party sources listed in scripts/deps.txt into src/.
# Each line of deps.txt is: <name> <git url> <commit> [# comment]. A directory that already exists is left alone.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$W/src"
while read -r name url commit _; do
	case $name in ''|'#'*) continue;; esac
	if [ -n "$(ls -A "$W/src/$name" 2>/dev/null)" ]; then
		echo "have src/$name"; continue
	fi
	echo "fetching $name @ ${commit:0:10}"
	mkdir -p "$W/src/$name"
	git -C "$W/src/$name" init -q
	git -C "$W/src/$name" fetch -q --depth 1 "$url" "$commit"
	git -C "$W/src/$name" checkout -q FETCH_HEAD
done < "$W/scripts/deps.txt"
