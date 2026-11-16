#!/usr/bin/env python3
"""qmp-hmp.py CMD... -- send HMP monitor commands to /tmp/hs-monitor.sock one at a time and wait for the prompt (no readline mangling)."""
import socket, sys, time, re
def run(cmds, path='/tmp/hs-monitor.sock', timeout=600):
    s = socket.socket(socket.AF_UNIX); s.connect(path); s.settimeout(timeout)
    def rd():
        buf = b''
        while not buf.endswith(b'(qemu) '):
            d = s.recv(65536)
            if not d: break
            buf += d
        return buf
    rd()
    for c in cmds:
        s.sendall(c.encode() + b'\n'); r=rd().decode(errors='replace'); r=re.sub(r'\x1b\[[0-9;]*[A-Za-z]|\x08','',r); print('>', c, '->', r.split('\n')[-3:] )
if __name__ == '__main__': run(sys.argv[1:])
