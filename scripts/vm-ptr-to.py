#!/usr/bin/env python3
import os
"""vm-ptr-to.py X Y [--click]   Closed-loop pointer positioning for the Tahoe VM (relative usb-mouse on the QEMU monitor).
Locates the cursor by diffing screenshots before/after a small nudge (the tip is the top-left of the diff box), then
moves by the measured error until within TOL pixels. Requires a relative mouse (see vm-ptr.sh)."""
import socket, subprocess, sys, time
from PIL import Image, ImageChops
S = '/tmp/hs-monitor.sock'
SHOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vm-shot.sh')
def mon(cmd):
    c = socket.socket(socket.AF_UNIX); c.connect(S); c.sendall((cmd + '\n').encode()); time.sleep(0.08); c.close()
def shot(p):
    subprocess.run([SHOT, p], check=True)
def move(dx, dy, step=14):
    n = max(abs(dx), abs(dy)) // step + 1
    for i in range(n):
        mon('mouse_move %d %d' % (round(dx / n), round(dy / n))); time.sleep(0.12)
    time.sleep(3.5)
def locate():
    shot('/tmp/pa.png'); move(16, 16); shot('/tmp/pb.png')
    d = ImageChops.difference(Image.open('/tmp/pa.png').convert('L'), Image.open('/tmp/pb.png').convert('L')).point(lambda v: 255 if v > 20 else 0)
    from PIL import ImageDraw
    ImageDraw.Draw(d).rectangle([405, 340, 900, 512], fill=0)   # ignore blinking text carets in form fields
    bb = d.getbbox()
    return None if not bb else (bb[0], bb[1])   # tip of the OLD position; the cursor has since moved ~(+10,+10)
def main():
    tx, ty = int(sys.argv[1]), int(sys.argv[2])
    tol = 9

    for it in range(8):
        p = locate()
        if p is None:
            print('cursor not found'); sys.exit(1)
        cx, cy = p[0] + 10, p[1] + 10        # approx current tip after the locate nudge
        ex, ey = tx - cx, ty - cy
        print('iter', it, 'cursor~', (cx, cy), 'err', (ex, ey))
        if abs(ex) <= tol and abs(ey) <= tol:
            break
        move(round(ex / 0.62), round(ey / 0.62))
    if '--click' in sys.argv:
        mon('mouse_button 1'); time.sleep(0.15); mon('mouse_button 0')
main()
