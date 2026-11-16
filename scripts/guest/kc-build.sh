#!/bin/sh
# kc-build.sh -- Recovery-side implementation of docs/cleanroom/kc-rebuild-spec.md (steps P1-P4, B1-B6).
# Runs inside the Tahoe Recovery Terminal (fetched from the host agent). Writes a log to /tmp/kc-build.out and uploads it.
# Environment (all optional): SUFFIX=development  VARIANT=release  KERNEL_URL=http://10.0.2.2:8000/files/kernel.xcross2
O=/tmp/kc-build.out; : > $O
SUFFIX=${SUFFIX:-}; VARIANT=${VARIANT:-release}   # empty SUFFIX = unsuffixed names (see spec B4: suffix selection is not reliable)
SX=${SUFFIX:+.$SUFFIX}
KERNEL_URL=${KERNEL_URL:-http://10.0.2.2:8000/files/kernel.xcross2}
{
VOL=/private/tmp/sysmnt; mkdir -p $VOL   # canonical path: /tmp is a symlink, and kmutil only strips the -R prefix from bundle paths when it matches the real path
DATA="/Volumes/Tahoe - Data"; KCDIR=$VOL/System/Library/KernelCollections
EXT=$VOL/System/Library/Extensions; DONOR="$DATA/kdk-store/kdk/System/Library/Extensions"
SCR="$VOL/.kcscratch"      # must live inside $VOL: with -R, -B/-S paths are resolved under the volume root
RSCR=/.kcscratch

# --- mount: System volume read-write, Preboot mounted
umount /Volumes/Tahoe 2>/dev/null
diskutil mount disk3s2 2>&1 | tail -1
mount | grep -q " $VOL " || mount -o nobrowse -t apfs /dev/disk3s4 $VOL || { echo "FATAL: cannot mount System volume rw"; exit 1; }
PREBOOT=$(ls -d /Volumes/Preboot/*/boot/System/Library/KernelCollections | head -1)
echo "preboot dir: $PREBOOT"

# --- P4: backup of the stock collections (once)
mkdir -p "$DATA/kcbackup"
[ -f "$DATA/kcbackup/BootKernelExtensions.kc" ] || cp $KCDIR/BootKernelExtensions.kc $KCDIR/SystemKernelExtensions.kc "$DATA/kcbackup/"
for e in BootKernelExtensions.kc.elides SystemKernelExtensions.kc.elides; do [ -f "$DATA/kcbackup/$e" ] || cp "$KCDIR/$e" "$DATA/kcbackup/" 2>/dev/null; done   # stock elides too (first run only)


# --- P1/P2: merge donor Mach-O files that the volume's bundles lack
echo "== P2 merge"
cd "$DONOR" || { echo "FATAL: donor tree missing"; exit 1; }
find . -type f ! -path '*.dSYM/*' ! -path './System.kext/*' | while read f; do
  [ -e "$EXT/$f" ] && continue
  bundle=$(echo "$(dirname "$f")" | sed 's|\(.*\.kext\).*|\1|')
  [ -d "$EXT/$bundle" ] || continue
  case "$(head -c 4 "$f" | od -An -tx1 | tr -d ' ')" in cffaedfe|cafebabe|bebafeca) ;; *) continue;; esac
  mkdir -p "$EXT/$(dirname "$f")"; cp "$f" "$EXT/$f"
done
# pseudo-kexts (com.apple.kpi.*): executable sits directly in the bundle
if [ -d /tmp/skr/System.kext/PlugIns ]; then
  for p in /tmp/skr/System.kext/PlugIns/*.kext; do n=$(basename "$p" .kext)
    [ -f "$p/$n" ] && [ -d "$EXT/System.kext/PlugIns/$n.kext" ] && cp "$p/$n" "$EXT/System.kext/PlugIns/$n.kext/$n"
  done
fi
cd /
echo "volume Mach-O files: $(find $EXT -type f -perm -u+x 2>/dev/null | wc -l)"

# --- kernel
KERNEL=$VOL/System/Library/Kernels/kernel.development
curl -s -o "$KERNEL" "$KERNEL_URL" && ls -la "$KERNEL"

# --- B1: boot collection
rm -rf "$SCR"; mkdir -p "$SCR"
COMMON="-a x86_64 -R $VOL -k $KERNEL --allow-missing-kdk -V $VARIANT -z --no-variant-extension"
echo "== B1 boot"
kmutil create -n boot $COMMON -B "$RSCR/boot.kc" 2>&1 | grep -vE "KextExcludeList|^(time to|Merged|weak|exports|bindings|lazy|function|data in|symbol|LINKEDIT)" | head -20
[ -s "$SCR/boot.kc" ] || { echo "FATAL: boot collection not produced"; }

# --- B2: elide list = every member of the boot collection except the kernel
kmutil inspect -B "$SCR/boot.kc" --no-header 2>&1 | awk -F'\t' 'NF>=3 && $1!="com.apple.kernel" {print $1}' | sort -u > "$SCR/elides"
echo "boot members (excluding kernel): $(wc -l < "$SCR/elides")"

# --- B3: system collection, eliding the boot members
set --
for id in $(cat "$SCR/elides"); do set -- "$@" --elide-identifier "$id"; done
echo "== B3 system"
kmutil create -n sys $COMMON -B "$RSCR/boot.kc" -S "$RSCR/sys.kc" "$@" 2>&1 | grep -vE "KextExcludeList|^(time to|Merged|weak|exports|bindings|lazy|function|data in|symbol|LINKEDIT)" | head -20

# --- B3b: kmutil -R records the build-time mount path (e.g. /private/tmp/sysmnt/System/Library/Extensions/X.kext) in each collection, and
#     kernelmanagerd later resolves kext bundles (resource requests) through it. Rewrite the prefix in place with a same-length path that
#     resolves to "/" at runtime: "/" + "./"*k (+ "."), so no byte offset in the collection changes.
echo "== B3b rewrite build-mount prefix"
for f in "$SCR/boot.kc" "$SCR/sys.kc"; do
  echo "before: $(grep -ac 'tmp/sysmnt' "$f" 2>/dev/null) lines with the build path in $(basename $f)"
  perl -pi -e 's{(?<![\w/.])((?:/[\w.]+)*?/tmp/sysmnt)(?=/)}{my $n=length($1); "/" . ("./" x int(($n-1)/2)) . ((($n-1)%2) ? "." : "")}ge' "$f"
  echo "after:  $(grep -ac 'tmp/sysmnt' "$f" 2>/dev/null)"
done
grep -a -o '[^<>"]\{0,30\}\(\./\)\{4,\}[^<>"]\{0,40\}' "$SCR/sys.kc" 2>/dev/null | head -2
ls -la "$SCR"

# --- B4: install (boot -> volume + Preboot, system -> volume only). Any leftover .development collections are moved out of the way:
#     boot.efi loads BootKernelExtensions.kc.development whenever it exists, whatever kcsuffix says (observed).
mkdir -p "$DATA/kcbackup/off"
for f in "$KCDIR"/*.development* "$PREBOOT"/*.development*; do [ -e "$f" ] && mv "$f" "$DATA/kcbackup/off/$(basename "$f")"; done
if [ -s "$SCR/boot.kc" ] && [ -s "$SCR/sys.kc" ]; then
  cp "$SCR/boot.kc" "$KCDIR/BootKernelExtensions.kc$SX";   cp "$SCR/elides" "$KCDIR/BootKernelExtensions.kc$SX.elides"
  cp "$SCR/sys.kc"  "$KCDIR/SystemKernelExtensions.kc$SX"; cp "$SCR/elides" "$KCDIR/SystemKernelExtensions.kc$SX.elides"
  cp "$SCR/boot.kc" "$PREBOOT/BootKernelExtensions.kc$SX"; cp "$SCR/elides" "$PREBOOT/BootKernelExtensions.kc$SX.elides"
  echo "installed custom collections as BootKernelExtensions.kc$SX / SystemKernelExtensions.kc$SX (stock pair stays in kcbackup)"
else
  echo "NOT installed: one of the collections is missing"
fi
# --- B5b: new bootable snapshot (the system boots from a snapshot, not the live volume)
echo "== B5b snapshot"
bless --folder "$VOL/System/Library/CoreServices" --bootefi --create-snapshot 2>&1 | tail -4
diskutil apfs listSnapshots disk3s4 2>&1 | grep -E "Name:|XID" | tail -4
# --- checks C1/C2
echo "== checks"
if [ -s "$KCDIR/SystemKernelExtensions.kc$SX" ]; then
  kmutil inspect -S "$KCDIR/SystemKernelExtensions.kc$SX" --no-header 2>&1 | awk -F'\t' 'NF>=3 {print $1}' | sort -u > "$SCR/sysmembers"
  echo "C1 boot members: $(wc -l < "$SCR/elides")  corecrypto: $(grep -c kec.corecrypto "$SCR/elides")"
  echo "C3 build-mount path leaked into collections (must be 0): boot=$(grep -ac 'tmp/sysmnt' "$KCDIR/BootKernelExtensions.kc$SX") sys=$(grep -ac 'tmp/sysmnt' "$KCDIR/SystemKernelExtensions.kc$SX")"
  echo "C2 system members: $(wc -l < "$SCR/sysmembers")  overlap with boot: $(comm -12 "$SCR/elides" "$SCR/sysmembers" | wc -l)"
fi
ls -la $KCDIR "$PREBOOT" | grep -v im4m
rm -rf "$SCR"
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/kc-build.out
echo uploaded
