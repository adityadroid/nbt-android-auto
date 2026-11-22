#!/bin/sh
# Car data (CDS) probe for the Car Health page (PLAN-diagnostics.md, phase 1). The probe is built into the HMI plugin and
# off unless its flag file is in the package folder.
#   scripts/cds-probe.sh on      turn it on (then open Multimedia -> Android Auto once; it starts then)
#   scripts/cds-probe.sh off     turn it off
#   scripts/cds-probe.sh status  flag present? probe running? log size, and the property that crashed iDrive (if any)
# Asks for the NBT root password. NBT_HOST changes the address (default 169.254.199.99).
NBT=${NBT_HOST:-169.254.199.99}
P=/var/opt/hmi/data/rw/NbtAndroidAutoRT
SSH="ssh -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa -o ConnectTimeout=10 root@$NBT"
case "$1" in
  on)  $SSH "touch $P/cds-probe && echo 'CDS probe ON: now open Multimedia -> Android Auto (leave with MENU and reopen if it is open)'" ;;
  off) $SSH "rm -f $P/cds-probe && echo 'CDS probe OFF'" ;;
  status) $SSH "test -e $P/cds-probe && echo 'flag: on' || echo 'flag: off';
      if test -e /fs/ram3/nbt-aa-cds-probe.log; then echo \"log: \`wc -c < /fs/ram3/nbt-aa-cds-probe.log\` bytes, \`wc -l < /fs/ram3/nbt-aa-cds-probe.log\` lines\"; else echo 'log: none yet (open the Android Auto page)'; fi;
      test -e /fs/ram3/nbt-aa-cds-probe-trying && echo \"crash guard: iDrive died while subscribing \`cat /fs/ram3/nbt-aa-cds-probe-trying\` - probe off until reboot\"" ;;
  *) echo "usage: $0 on|off|status"; exit 2 ;;
esac
