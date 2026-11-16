#!/bin/bash
# guest-run.sh '<shell command>' [timeout-seconds]
# Queues a command for the guest agent, waits for its output and prints it.
A="$(cd "$(dirname "$0")/../build/agent" && pwd)"
id=$(date +%s%N); printf '%s\n' "$1" > "$A/jobs/$id"
t=${2:-120}; for ((i=0;i<t;i++)); do [ -f "$A/results/$id" ] && { cat "$A/results/$id"; exit 0; }; sleep 1; done
echo "[guest-run] timed out waiting for job $id" >&2; exit 1
