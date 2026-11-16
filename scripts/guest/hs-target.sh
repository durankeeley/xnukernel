#!/bin/sh
# hs-target.sh [volume] -- run in the High Sierra Recovery Terminal (running on our kernel), after installer stage 1 and before stage 2.
#   - copies our kernel and prelinked kernel (the ones this Recovery runs) to the target's System/Library/{Kernels,PrelinkedKernels}, so that the
#     installed system can boot on our kernel right away (Apple's cannot boot on an AMD CPU). Recovery takes no input once the install has finished,
#     so this has to happen now. UNTESTED: whether these files, and the agent below, survive stage 2. The kernel copy that was proven is hs-single.sh.
# Makes the install volume's stage 2 boot use our kernel:
#   - copies prelinkedkernel.xcross into "macOS Install Data/Locked Files/Boot Files" and points com.apple.Boot.plist at it
#   - swaps auth-root-dmg= for root-dmg= : stage 2 loads BaseSystem.dmg as its root and normally authenticates the chunklist against Apple's
#     production public keys, which the open source xnu does not have (panic "root image authentication failed (err = 22)")
O=/tmp/hs-target.out; : > $O
{
V=${1:-/Volumes/Macintosh}; B="$V/macOS Install Data/Locked Files/Boot Files"
cp /System/Library/PrelinkedKernels/prelinkedkernel.xcross "$B/prelinkedkernel.xcross"; chmod 644 "$B/prelinkedkernel.xcross"
sed -i '' -e 's#Boot Files/prelinkedkernel</string>#Boot Files/prelinkedkernel.xcross</string>#' -e 's#auth-root-dmg=#root-dmg=#' "$B/com.apple.Boot.plist"
cat "$B/com.apple.Boot.plist"
mkdir -p "$V/System/Library/Kernels" "$V/System/Library/PrelinkedKernels"
cp /System/Library/Kernels/kernel.xcross "$V/System/Library/Kernels/"; chmod 755 "$V/System/Library/Kernels/kernel.xcross"
cp /System/Library/PrelinkedKernels/prelinkedkernel.xcross "$V/System/Library/PrelinkedKernels/"; chmod 644 "$V/System/Library/PrelinkedKernels/prelinkedkernel.xcross"
ls -l "$V/System/Library/Kernels" "$V/System/Library/PrelinkedKernels"
# the command agent (scripts/guest-agent.sh), if the installer leaves /Library alone
mkdir -p "$V/Library/xcross" "$V/Library/LaunchDaemons"
curl -s http://10.0.2.2:8000/files/guest-agent.sh -o "$V/Library/xcross/agent.sh" && chmod 755 "$V/Library/xcross/agent.sh"
cat > "$V/Library/LaunchDaemons/com.xcross.agent.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.agent</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>/Library/xcross/agent.sh</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
</dict></plist>
PL
chown root:wheel "$V/Library/LaunchDaemons/com.xcross.agent.plist"; chmod 644 "$V/Library/LaunchDaemons/com.xcross.agent.plist"
sync; echo "HS-TARGET-DONE"
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/hs-target.out
