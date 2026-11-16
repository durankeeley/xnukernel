#!/bin/bash
# vm.sh key <qemu-key>... | shot | type <text> | hotkbd | wake   (wake: pointer event so the GUI accepts keys)
S=/tmp/hs-monitor.sock
m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$S >/dev/null 2>&1; }
case $1 in
 key) shift; for k in "$@"; do m "sendkey $k"; sleep 0.3; done;;
 type) shift; t="$*"; for ((i=0;i<${#t};i++)); do c="${t:i:1}"; case "$c" in
   " ") k=spc;; "-") k=minus;; ".") k=dot;; "/") k=slash;; ":") k=shift-semicolon;; ";") k=semicolon;; "+") k=shift-equal;; "=") k=equal;; "_") k=shift-minus;; ",") k=comma;; "\\") k=backslash;; "'"|"\"") k=apostrophe;; "*") k=shift-8;; "&") k=shift-7;; "|") k=shift-backslash;; ">") k=shift-dot;; "<") k=shift-comma;; "~") k=shift-grave_accent;; "(") k=shift-9;; ")") k=shift-0;; "\$") k=shift-4;; "#") k=shift-3;; "@") k=shift-2;; "!") k=shift-1;; "%") k=shift-5;; "?") k=shift-slash;; [A-Z]) k="shift-${c,,}";; *) k="$c";; esac; m "sendkey $k"; sleep 0.15; done;;
 click) x=$(( $2*32767/1280 )); y=$(( $3*32767/800 )); m "mouse_move $x $y"; sleep 0.3; m "mouse_button 1"; sleep 0.1; m "mouse_button 0";;
 dclick) $0 click $2 $3; sleep 0.15; $0 click $2 $3;;
 waitpicker) D=$(dirname "$0"); t=${2:-300}; for ((i=0;i<t;i+=4)); do rm -f /tmp/hs.ppm /tmp/hs.png; $0 shot 2>/dev/null; [ -f /tmp/hs.png ] || { sleep 4; continue; }; python3 -W ignore - "$D/picker-ref.png" <<'PY' && exit 0
import sys
from PIL import Image, ImageChops, ImageStat
a=Image.open('/tmp/hs.png').convert('L').crop((400,300,860,480)); b=Image.open(sys.argv[1]).convert('L').crop((400,300,860,480))
sys.exit(0 if sum(ImageStat.Stat(ImageChops.difference(a,b)).mean)<6 else 1)
PY
sleep 4; done; echo "picker not seen" >&2; exit 1;;
 pickmac) for ((n=0;n<12;n++)); do rm -f /tmp/hs.png; $0 shot 2>/dev/null; if python3 -W ignore -c "
from PIL import Image; import sys
im=Image.open('/tmp/hs.png').convert('L'); sys.exit(0 if im.getpixel((748,325))>60 else 1)"; then sleep 1; $0 key ret; exit 0; fi; $0 key right; sleep 1.5; done; echo "could not highlight Macintosh" >&2; exit 1;;
 waitlogin) D=$(dirname "$0"); t=${2:-1800}; for ((i=0;i<t;i+=10)); do rm -f /tmp/hs.png /tmp/hs.ppm; $0 shot 2>/dev/null; [ -f /tmp/hs.png ] && python3 -W ignore - "$D/login-ref.png" <<'PY' && exit 0
import sys
from PIL import Image, ImageChops, ImageStat
a=Image.open('/tmp/hs.png').convert('L').crop((540,250,740,440)); b=Image.open(sys.argv[1]).convert('L').crop((540,250,740,440))
sys.exit(0 if sum(ImageStat.Stat(ImageChops.difference(a,b)).mean)<5 else 1)
PY
sleep 10; done; echo "login window not seen" >&2; exit 1;;
 hotkbd) m "device_add usb-kbd,id=kb$(date +%s),bus=ehci.0"; sleep 5;;
 wake) m "device_add usb-mouse,id=ms$(date +%s),bus=ehci.0"; sleep 6; for i in 1 2 3; do m "mouse_move 5 5"; sleep 0.3; m "mouse_move -5 -5"; sleep 0.3; done;;   # a guest GUI session ignores the keyboard until it sees a pointer event (the display is asleep): hot-plug a relative mouse and move it   # the guest's USB keyboard stalls after a while; a freshly hot-plugged one works
 shot) m "screendump /tmp/hs.ppm"; sleep 1; python3 -c "from PIL import Image; Image.open('/tmp/hs.ppm').save('/tmp/hs.png')";;
esac
