#!/bin/bash
# Regenerate the iig outputs (DriverKit headers + *.iig.cpp) and the Tahoe SDK headers after changing toolchain/bin/iig.
# make does not track the iig script as a dependency, so the generated files are deleted first.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
O=$W/build/xnu-tahoe/obj
find "$O" -path '*DriverKit/*' \( -name '*.h' -o -name '*.iig.cpp' -o -name '*.cpo' \) -not -path '*/EXPORT_HDRS/*' -delete 2>/dev/null || true
rm -rf "$O/EXPORT_HDRS/iokit/DriverKit"
"$W/scripts/xnu-make.sh" installhdrs > "$W/build/tahoe-installhdrs.log" 2>&1
chmod -R u+w "$W/toolchain/sdk"
cp -a "$W/build/xnu-tahoe/dst/." "$W/toolchain/sdk/"
echo "iig outputs regenerated"
