#!/bin/sh
# install-diag-daemon.sh -- (Recovery) install a LaunchDaemon on the Data volume that, after each boot, periodically records what userland is
# doing (ps with wait channels, launchctl, WindowServer/loginwindow/kernelmanagerd log excerpts, sample of WindowServer/loginwindow, a screen
# capture, ioreg once) into /var/tmp/xcross-diag and uploads every file to the host agent-server (http://10.0.2.2:8000/upload/).
# Remove again with: rm "$DATA/Library/LaunchDaemons/com.xcross.diag.plist"
O=/tmp/install-diag-daemon.out; : > $O
{
DATA="/Volumes/Tahoe - Data"
mkdir -p "$DATA/usr/local/xcross" "$DATA/Library/LaunchDaemons"
cat > "$DATA/usr/local/xcross/diag.sh" <<'EOS'
#!/bin/sh
# runs as root from launchd; ~40 rounds, 30 s apart
D=/var/tmp/xcross-diag; mkdir -p $D
BOOT=$(date +%H%M%S)
up() { # name file
  curl -s -m 30 -T "$2" "http://10.0.2.2:8000/upload/dg-$BOOT-$1" >/dev/null 2>&1
}
N=0
while [ $N -lt 40 ]; do
  N=$((N+1)); T=$D/r$N; mkdir -p $T
  date > $T/date
  uptime >> $T/date
  ps -axo pid,ppid,stat,wchan,etime,time,command > $T/ps 2>&1
  launchctl list > $T/launchctl 2>&1
  if [ $N = 1 ] || [ $N = 4 ] || [ $N = 12 ]; then
    ioreg -l -w0 > $T/ioreg 2>&1
    ioreg -c IOFramebuffer -r -w0 > $T/ioreg-fb 2>&1
    ioreg -c IOAccelerator -r -w0 > $T/ioreg-accel 2>&1
    /usr/bin/perl -e 'alarm 20; exec @ARGV' kmutil showloaded > $T/kexts 2>&1; /usr/bin/perl -e 'alarm 20; exec @ARGV' kextstat > $T/kextstat 2>&1
  fi
  log show --style compact --last 10m --predicate 'process == "kernelmanagerd"' 2>&1 | head -400 > $T/kmd-log
  log show --style compact --last 90s --predicate 'process == "WindowServer" OR process == "loginwindow" OR process == "kernelmanagerd" OR process == "SecurityAgent" OR process == "launchd"' 2>&1 | tail -150 > $T/log
  for P in WindowServer loginwindow; do
    PID=$(pgrep -x $P | head -1)
    [ -n "$PID" ] && sample $PID 2 -file $T/sample-$P.txt >/dev/null 2>&1
  done
  # processes in uninterruptible sleep (U): user stack shows the syscall they are blocked in; open files show what they wait on
  for UP in $(ps -axo pid,stat | awk '$2 ~ /^U/ {print $1}' | head -6); do
    sample $UP 1 -file $T/sample-U$UP.txt >/dev/null 2>&1
    lsof -p $UP > $T/lsof-U$UP.txt 2>&1
  done
  # kernel stack dump (serial console): every blocked thread of usermanagerd, then of all processes
  UMP=$(pgrep -x usermanagerd | head -1); [ -n "$UMP" ] && [ $N -le 2 ] && sysctl -w debug.xcross_dump=$UMP > $T/xdump.out 2>&1
  [ $N = 2 ] && sysctl -w debug.xcross_dump=0 >> $T/xdump.out 2>&1
  if [ $N = 1 ] || [ $N = 3 ]; then
    # renameatx_np (syscall 488) with each flag, in a normal dir and in keybags; each run is backgrounded and given 8 s so a hang cannot stall the daemon
    R=$T/rename-tests; : > $R
    rx() { # dir flags
      D=$1; F=$2; cd $D || return
      echo src > xcr-a; echo dst > xcr-b; rm -f xcr-res
      ( /usr/bin/perl -e 'my $r=syscall(488,-2,$ARGV[0],-2,$ARGV[1],($ARGV[2]+0)); print "ret=$r errno=".($!+0)."\n"' xcr-a xcr-b $F > xcr-res 2>&1 ) &
      sleep 8
      if [ -s xcr-res ]; then echo "$D flags=$F: $(cat xcr-res)" >> $R; else echo "$D flags=$F: HUNG (no result after 8 s)" >> $R; fi
      rm -f xcr-a xcr-b xcr-res; cd /
    }
    for F in 0 1 2 4 16 20 21; do rx /private/tmp $F; done
    for F in 0 2 4 16 20; do rx /private/var/db/keybags $F; done
    ls -laO@ /private/var/db/keybags >> $R 2>&1
  fi

  [ $((N % 3)) = 1 ] && screencapture -x -t png $T/screen.png 2>/dev/null
  for f in $T/*; do up "r$N-$(basename $f)" "$f"; done
  sleep 30
done
EOS
chmod 755 "$DATA/usr/local/xcross/diag.sh"
cat > "$DATA/Library/LaunchDaemons/com.xcross.diag.plist" <<'EOP'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.diag</string>
<key>ProgramArguments</key><array><string>/bin/sh</string><string>/usr/local/xcross/diag.sh</string></array>
<key>RunAtLoad</key><true/>
<key>StandardOutPath</key><string>/var/tmp/xcross-diag.out</string>
<key>StandardErrorPath</key><string>/var/tmp/xcross-diag.err</string>
</dict></plist>
EOP
chown root:wheel "$DATA/Library/LaunchDaemons/com.xcross.diag.plist"; chmod 644 "$DATA/Library/LaunchDaemons/com.xcross.diag.plist"
chown -R root:wheel "$DATA/usr/local/xcross"

# --- independent stack-dump daemon (kernel prints the stacks of usermanagerd's blocked threads on the serial console)
cat > "$DATA/usr/local/xcross/dump.sh" <<'EOS2'
#!/bin/sh
sleep 90; N=0
while [ $N -lt 8 ]; do
  N=$((N+1)); P=$(pgrep -x usermanagerd | head -1)
  # each call is backgrounded: a call that blocks in the kernel must not stop the later ones
  [ -n "$P" ] && { /usr/sbin/sysctl -w debug.xcross_dump=$P >> /var/tmp/xcross-dump.out 2>&1 & }
  [ $N = 3 ] && [ -n "$P" ] && { /usr/sbin/sysctl -w debug.xcross_dump=$((P + 1073741824)) >> /var/tmp/xcross-dump.out 2>&1 & }
  [ $N = 4 ] && { /usr/sbin/sysctl -w debug.xcross_dump=0 >> /var/tmp/xcross-dump.out 2>&1 & }
  sleep 40
done
EOS2
chmod 755 "$DATA/usr/local/xcross/dump.sh"
cat > "$DATA/Library/LaunchDaemons/com.xcross.dump.plist" <<'EOP2'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.xcross.dump</string>
<key>ProgramArguments</key><array><string>/bin/sh</string><string>/usr/local/xcross/dump.sh</string></array>
<key>RunAtLoad</key><true/>
</dict></plist>
EOP2
chown root:wheel "$DATA/Library/LaunchDaemons/com.xcross.dump.plist"; chmod 644 "$DATA/Library/LaunchDaemons/com.xcross.dump.plist"; chown -R root:wheel "$DATA/usr/local/xcross"
ls -la "$DATA/Library/LaunchDaemons/com.xcross.diag.plist" "$DATA/usr/local/xcross/diag.sh"
VOL=/private/tmp/sysmnt; umount /Volumes/Tahoe 2>/dev/null; mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse,rdonly -t apfs /dev/disk3s4 $VOL
B=$VOL/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext
echo "== keybags dir"; ls -laO@ "$DATA/private/var/db/keybags" 2>&1 | head -20
echo "== plugin bundle on the system volume"; ls -laR "$B" | head -30
echo "== plugin bundle in the data-volume KDK copy"; ls -la "$DATA/kdk-store/kdk/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext/Contents/Resources" | head
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/install-diag-daemon.out
echo uploaded
