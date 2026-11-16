#!/bin/sh
# collect-diag.sh -- find spindump/crash/diagnostic reports written by the last boot(s) on the Data volume and upload them as one tarball.
O=/tmp/collect-diag.out; : > $O
{
DATA="/Volumes/Tahoe - Data"
echo "== candidate report files (modified in the last 24h)"
find "$DATA/Library/Logs" "$DATA/private/var/log" "$DATA/private/var/db/diagnostics" "$DATA/private/var/tmp" "$DATA/private/var/root/Library/Logs" \
  -type f -mmin -1440 \( -name '*.spin' -o -name '*.ips' -o -name '*.crash' -o -name '*.diag' -o -name '*.panic' -o -name '*.txt' -o -name 'system.log*' \) 2>/dev/null | head -60
find "$DATA" -maxdepth 6 -type f -mmin -1440 \( -name '*kernelmanagerd*' -o -name '*spindump*' -o -name '*.spin' \) 2>/dev/null | head -30
} >> $O 2>&1
tar -C "$DATA" -czf /tmp/diag.tgz Library/Logs/IOKit Library/Logs/DiagnosticReports private/var/log/system.log 2>/dev/null
ls -la /tmp/diag.tgz >> $O
curl -s -T /tmp/diag.tgz http://10.0.2.2:8000/upload/diag.tgz
curl -s -T $O http://10.0.2.2:8000/upload/collect-diag.out
echo uploaded
