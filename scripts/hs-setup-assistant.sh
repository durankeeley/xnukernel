#!/bin/bash
# hs-setup-assistant.sh -- click through High Sierra's Setup Assistant in the VM and create the account xnu / xnu.
# Run it when the "Welcome" (country) screen is showing. Uses scripts/vm.sh wake for a relative mouse and clicks by screen position:
# the relative mouse moves about 1.75 times further than asked, so a click at screen (x,y) sends the point times 0.572 after homing the pointer
# to the top left corner. Screenshots of each screen go to build/shots/setup/.
cd "$(dirname "$0")/.."
S=build/shots/setup; mkdir -p $S
M=/tmp/hs-monitor.sock; m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$M >/dev/null 2>&1; }
click() { for i in 1 2 3 4 5 6 7 8; do m "mouse_move -300 -300"; sleep 0.15; done
  m "mouse_move $(( $1*572/1000 )) $(( $2*572/1000 ))"; sleep 0.8; m "mouse_button 1"; sleep 0.2; m "mouse_button 0"; }
shot() { scripts/vm-shot.sh $S/$1.png >/dev/null 2>&1; }
step() { click "$2" "$3"; sleep "${4:-12}"; shot "$1"; echo "$1"; }
scripts/vm.sh wake
click 540 438; sleep 1; scripts/vm.sh hotkbd; scripts/vm.sh type "United S"; sleep 2
step 01-country 688 643          # Continue
step 02-keyboard 688 643
step 03-privacy 688 643
step 04-transfer 688 643         # "Don't transfer" is the default
step 05-appleid 640 226 6        # Set Up Later
step 06-appleid-skip 790 206 10  # Skip
step 07-terms 688 643 6          # Agree
step 08-terms-sheet 790 200 12   # Agree in the sheet
click 624 302; sleep 1; scripts/vm.sh hotkbd; scripts/vm.sh type "xnu"; sleep 1
scripts/vm.sh key tab; sleep 1; scripts/vm.sh key tab; sleep 1; scripts/vm.sh type "xnu"; sleep 1; scripts/vm.sh key tab; sleep 1; scripts/vm.sh type "xnu"; sleep 3; shot 09-account
click 688 643; echo "account created, waiting for Express Set Up"; sleep 100; scripts/vm.sh wake; shot 10-express
step 11-desktop 688 643 25
echo "done, screenshots in $S"
