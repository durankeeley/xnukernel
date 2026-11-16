#!/bin/bash
# oc-set.sh '<boot-args>' <csr-hex e.g. 0x7f>   (VM must be OFF)
# Edits NVRAM boot-args and csr-active-config in OSX-KVM/OpenCore/OpenCore.qcow2 via qemu-nbd.
set -eu
IMG=${OSXKVM:-$(cd "$(dirname "$0")/../.." && pwd)/OSX-KVM}/OpenCore/OpenCore.qcow2
pgrep -x qemu-system-x86 >/dev/null && { echo "stop the VM first" >&2; exit 1; }
sudo -n modprobe nbd max_part=8
sudo -n qemu-nbd -d /dev/nbd0 >/dev/null 2>&1 || true
sudo -n qemu-nbd -c /dev/nbd0 "$IMG"; sleep 1
sudo -n mkdir -p /mnt/oc; mountpoint -q /mnt/oc || sudo -n mount /dev/nbd0p1 /mnt/oc
sudo -n python3 - "$1" "$2" <<'PY'
import plistlib, sys, struct
p='/mnt/oc/EFI/OC/config.plist'
d=plistlib.load(open(p,'rb'))
g='7C436110-AB2A-4BBB-A880-FE41995C9F82'
d['NVRAM']['Add'][g]['boot-args']=sys.argv[1]
d['NVRAM']['Add'][g]['csr-active-config']=struct.pack('<I', int(sys.argv[2],16))
dl=d['NVRAM']['Delete'].setdefault(g,[])
for k in ('boot-args','csr-active-config'):
    if k not in dl: dl.append(k)
plistlib.dump(d,open(p,'wb'),sort_keys=False)
print('boot-args =',d['NVRAM']['Add'][g]['boot-args'],' csr =',d['NVRAM']['Add'][g]['csr-active-config'].hex())
PY
sync; sudo -n umount /mnt/oc; sudo -n qemu-nbd -d /dev/nbd0 >/dev/null
