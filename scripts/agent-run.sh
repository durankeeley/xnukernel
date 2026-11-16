#!/bin/bash
# agent-run.sh '<shell commands>' [timeout_s] -- run commands as root in the running guest through the polling agent and print the output.
cd "$(dirname "$0")/.."; J=build/agent/jobs; R=build/agent/results; mkdir -p $J $R
id=$(date +%s%N); printf '%s\n' "$1" > $J/$id; T=${2:-90}; t=0
while [ $t -lt $T ]; do [ -s $R/$id ] && { cat $R/$id; exit 0; }; sleep 2; t=$((t+2)); done
echo "agent-run: no result after ${T}s (job $id still queued/taken?)"; exit 1
