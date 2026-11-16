#!/bin/sh
# Run in the macOS RECOVERY terminal:  curl -s http://10.0.2.2:8000/files/install-agent.sh | sh
# Installs the guest agent + LaunchDaemon on /Volumes/Macintosh and skips Setup Assistant.
T=${T:-/Volumes/Macintosh}
mkdir -p $T/Library/xcross
curl -s http://10.0.2.2:8000/files/guest-agent.sh -o $T/Library/xcross/agent.sh && chmod 755 $T/Library/xcross/agent.sh
cat > $T/Library/LaunchDaemons/com.xcross.agent.plist <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.agent</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>/Library/xcross/agent.sh</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
</dict></plist>
PL
chown root:wheel $T/Library/LaunchDaemons/com.xcross.agent.plist; chmod 644 $T/Library/LaunchDaemons/com.xcross.agent.plist
touch $T/var/db/.AppleSetupDone
ls -la $T/Library/xcross $T/Library/LaunchDaemons/com.xcross.agent.plist
