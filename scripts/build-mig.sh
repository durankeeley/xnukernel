#!/bin/bash
# Build Apple's open source MIG (src/bootstrap_cmds, version pinned in scripts/deps.txt) for the Linux host into toolchain/mig.
# XNU's Mach headers are used for constants; the few Darwin-only BSD headers
# they pull in are copied into toolchain/host-shim on demand (glibc keeps
# priority for everything it has).
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
S=$W/toolchain/host-shim
X=$W/src/${XNU_SRC:-xnu}
DEST=$W/${MIG_DEST:-toolchain/mig}
B=$W/build/mig${MIG_DEST:+-$(basename $MIG_DEST)}
MIGSRC=$W/src/${MIG_SRC:-bootstrap_cmds}/migcom.tproj
mkdir -p "$B" "$S" "$DEST"
cd "$MIGSRC"

bison -d -o "$B/parser.c" parser.y
cp "$B/parser.h" "$B/y.tab.h"	# lexxer.l includes the yacc name
flex -o "$B/lexxer.c" lexxer.l

# the sources Apple's mig.xcodeproj compiles
SRCS="error.c global.c header.c mig.c routine.c server.c statement.c string.c type.c user.c utils.c"
for i in $(seq 1 60); do
	if out=$(clang -O1 -w -D_DEFAULT_SOURCE -include sys/types.h -include "$S/xcross_mig_compat.h" -I. -I"$B" -I"$S" \
		-idirafter "$X/osfmk" -idirafter "$X/libkern" -idirafter "$X/EXTERNAL_HEADERS" -D__MACH30__ \
		'-DMIG_VERSION="${MIG_SRC:-bootstrap_cmds}"' -o "$B/migcom" $SRCS "$B/parser.c" "$B/lexxer.c" 2>&1); then
		break
	fi
	h=$(printf '%s\n' "$out" | sed -n "s/.*'\([^']*\)' file not found.*/\1/p" | head -1)
	if [ -n "$h" ] && [ -f "$X/bsd/$h" ]; then
		mkdir -p "$S/$(dirname "$h")"
		cp "$X/bsd/$h" "$S/$h"
		# glibc already provides the fixed-width integer types; Darwin's typedefs (long long) would clash
		case $(basename "$h") in _int*_t.h|_u_int*_t.h|_uintptr_t.h|_intptr_t.h) echo "/* xcross: glibc provides this */" > "$S/$h";; esac
		echo "  shim: $h"
	elif [ -n "$h" ] && [ -f "$X/libkern/$h" ]; then
		mkdir -p "$S/$(dirname "$h")"
		cp "$X/libkern/$h" "$S/$h"
		echo "  shim: $h (libkern)"
	else
		printf '%s\n' "$out" | grep -m5 error
		exit 1
	fi
done
cp "$B/migcom" "$DEST/migcom"
# mig is a shell script driver around the C preprocessor and migcom. Make it
# find migcom next to itself, not need /usr/bin/arch, and preprocess for a
# Darwin target (so __APPLE__, __MACH__ etc. are defined as on a Mac).
sed -e 's|^migcomPath=.*|migcomPath="${scriptRoot}/migcom"|' \
    -e 's|^arch=`/usr/bin/arch`|arch=x86_64|' \
    -e 's|"\$C" -E -arch \${arch}|"$C" -E -target ${arch}-apple-darwin17|' \
    -e 's|  xcrunPath="/usr/bin/xcrun"|  xcrunPath=/nonexistent|' \
    -e 's|    MIGCC=$(realpath "${scriptRoot}/cc")|    MIGCC=clang|' \
    mig.sh > "$DEST/mig"
chmod +x "$DEST/mig"
echo "built $DEST/mig and migcom"
