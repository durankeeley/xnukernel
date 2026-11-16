#!/bin/sh
# hs-account.sh -- run in the High Sierra Recovery Terminal after the install finished: create the test account xnu / xnu on the
# installed volume, install the command agent (scripts/install-agent.sh) and skip Setup Assistant.
O=/tmp/hs-account.out; : > $O
{
T=${T:-/Volumes/Macintosh}; N=$T/var/db/dslocal/nodes/Default
ls $T | head -20
dscl -f $N localonly -create /Users/xnu
dscl -f $N localonly -create /Users/xnu UserShell /bin/bash
dscl -f $N localonly -create /Users/xnu RealName "XNU Dev"
dscl -f $N localonly -create /Users/xnu UniqueID 501
dscl -f $N localonly -create /Users/xnu PrimaryGroupID 20
dscl -f $N localonly -create /Users/xnu NFSHomeDirectory /Users/xnu
dscl -f $N localonly -create /Users/xnu GeneratedUID "$(uuidgen)"
dscl -f $N localonly -passwd /Users/xnu xnu
dscl -f $N localonly -append /Groups/admin GroupMembership xnu
mkdir -p $T/Users/xnu; chown 501:20 $T/Users/xnu; chmod 755 $T/Users/xnu
dscl -f $N localonly -read /Users/xnu UniqueID NFSHomeDirectory AuthenticationAuthority
curl -s http://10.0.2.2:8000/files/install-agent.sh | sh
sync
echo "HS-ACCOUNT-DONE"
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/hs-account.out
