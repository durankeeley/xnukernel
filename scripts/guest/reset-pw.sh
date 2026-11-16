#!/bin/sh
# reset-pw.sh -- (Recovery) set the documented test password (xnu) for the local account xnu by editing the Data volume's local directory node offline.
# Needed when the login window rejects the password and the running system refuses a reset without the secure-token password.
O=/tmp/reset-pw.out; : > $O
{
DATA="/Volumes/Tahoe - Data"; NODE="$DATA/private/var/db/dslocal/nodes/Default"
ls -d "$NODE" 2>&1
dscl -f "$NODE" localonly -list /Users 2>&1 | grep -vE "^_|daemon|nobody|root"
dscl -f "$NODE" localonly -passwd /Users/xnu xnu 2>&1 | head -3
echo "rc=$?"
dscl -f "$NODE" localonly -read /Users/xnu AuthenticationAuthority 2>&1 | head -3
sync
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/reset-pw.out
echo uploaded
