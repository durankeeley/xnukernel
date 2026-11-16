#!/bin/sh
# install-agent-tahoe.sh -- (Recovery) install the polling guest agent as a LaunchDaemon on the Tahoe DATA volume.
# The agent (scripts/guest-agent.sh) polls the host job server (scripts/agent-server.py) and runs queued scripts as root.
O=/tmp/install-agent-tahoe.out; : > $O
{
DATA="/Volumes/Tahoe - Data"
mkdir -p "$DATA/Library/xcross"
curl -s http://10.0.2.2:8000/files/guest-agent.sh -o "$DATA/Library/xcross/agent.sh"; chmod 755 "$DATA/Library/xcross/agent.sh"
cat > "$DATA/Library/LaunchDaemons/com.xcross.agent.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.agent</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>/Library/xcross/agent.sh</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
</dict></plist>
PL
chown root:wheel "$DATA/Library/LaunchDaemons/com.xcross.agent.plist"; chmod 644 "$DATA/Library/LaunchDaemons/com.xcross.agent.plist"
chown -R root:wheel "$DATA/Library/xcross"
ls -la "$DATA/Library/xcross" "$DATA/Library/LaunchDaemons/com.xcross.agent.plist"
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/install-agent-tahoe.out
echo uploaded
