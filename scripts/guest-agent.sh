#!/bin/bash
# Runs inside the macOS guest as root (LaunchDaemon). Polls the host job server.
H=http://10.0.2.2:8000
while true; do
  hdr=$(mktemp /tmp/ah.XXXX); body=$(mktemp /tmp/ab.XXXX)
  code=$(curl -s -m 10 -D "$hdr" -o "$body" -w '%{http_code}' $H/next)
  if [ "$code" = 200 ]; then
    id=$(awk 'tolower($1)=="x-job-id:"{print $2}' "$hdr" | tr -d '\r')
    ( cd / && /bin/bash "$body" ) > "$body.out" 2>&1; echo "[exit $?]" >> "$body.out"
    curl -s -m 60 -T "$body.out" $H/result/$id >/dev/null
  fi
  rm -f "$hdr" "$body" "$body.out"; sleep 2
done
