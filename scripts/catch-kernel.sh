#!/bin/bash
# catch-kernel.sh  -- poll the serial log for the kernel's first PMAP line, then 'stop' the VM and dump IDT/CR3/RIP (via QEMU monitor).
S=/tmp/hs-monitor.sock; L=/tmp/hs-serial.log
m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$S 2>/dev/null; }
for i in $(seq 1 ${1:-6000}); do grep -aq "PMAP: Supervisor Mode Access" $L 2>/dev/null && break; sleep 0.05; done
m stop >/dev/null; sleep 0.3
m "info registers" | tr -d '\r' | grep -aE "RIP|IDT|CR3|GDT|CR0|CR4"
