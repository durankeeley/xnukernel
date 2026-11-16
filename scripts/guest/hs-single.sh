#!/bin/sh
# hs-single.sh <dir> -- run as root in SINGLE-USER mode (boot-args -s) on the freshly installed system, once, to put our kernel on it. Single-user has no
# network, so the files come from <dir>, a mounted FAT volume (the install disk's EFI partition) that holds kernel.xcross and guest-agent.sh:
#   /sbin/mount -uw /; mkdir /m; mount_msdos /dev/disk0s1 /m; sh /m/hs-single.sh /m
# Installs the command agent, and builds prelinkedkernel.xcross from the installed system's own kexts (kextd is not running, so -all-loaded means
# exactly the kexts of Apple's default prelinked kernel) and installs it with kernel.xcross. Run on a Penryn QEMU model: Apple's kernel is what boots
# there. It does not create an account: dscl -f needs a launchd job that single-user mode does not have, so the account is made in Setup Assistant.
D=${1:?usage: hs-single.sh <dir with kernel.xcross and guest-agent.sh>}
O=$D/hs-single.out; : > $O
{
mkdir -p /Library/xcross; cp $D/guest-agent.sh /Library/xcross/agent.sh; chmod 755 /Library/xcross/agent.sh
cat > /Library/LaunchDaemons/com.xcross.agent.plist <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.agent</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>/Library/xcross/agent.sh</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
</dict></plist>
PL
chown root:wheel /Library/LaunchDaemons/com.xcross.agent.plist; chmod 644 /Library/LaunchDaemons/com.xcross.agent.plist
mkdir -p /System/Library/Kernels /System/Library/PrelinkedKernels
cp $D/kernel.xcross /System/Library/Kernels/kernel.xcross; chmod 755 /System/Library/Kernels/kernel.xcross
kextcache -z -a x86_64 -K /System/Library/Kernels/kernel.xcross -no-authentication -all-loaded \
  -c /System/Library/PrelinkedKernels/prelinkedkernel.xcross /System/Library/Extensions 2>&1 | grep -v "dependency '" | tail -15
chmod 644 /System/Library/PrelinkedKernels/prelinkedkernel.xcross
ls -l /System/Library/Kernels /System/Library/PrelinkedKernels
sync; echo "HS-SINGLE-DONE"
} >> $O 2>&1
