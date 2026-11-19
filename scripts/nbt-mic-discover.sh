#!/bin/sh
# Read-only look at the NBT for the Android Auto car microphone and calls (aa_mic.cpp, CAR-TEST-aa-dev.md section 5).
# Run on the Mac with the car connected and SSH open:   scripts/nbt-mic-discover.sh
# Changes nothing on the car. Output: .local/car/mic-discover-<time>.txt (git-ignored: it may hold the car's Bluetooth
# address).
#   - natp audio scenarios and the endpoints they route the mic to (natp.cfg)
#   - the A2I endpoints dev-ioamp-router serves (capture endpoints start with output_)
#   - whether handsfreed (switches scenarios) runs on this NBT
#   - candidates for the car's own Bluetooth address
set -e
NBT=${NBT:-169.254.199.99}
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT="$REPO/.local/car/mic-discover-$(date +%Y%m%dT%H%M%S).txt"
mkdir -p "$REPO/.local/car"
ssh -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa -o ConnectTimeout=8 root@"$NBT" sh -s <<'EOS' | tee "$OUT"
echo "== natp config files"
ls -l /opt/speech/bin/*.cfg 2>&1
echo "== natp scenarios (names) and mic / uplink lines"
grep -niE "scenario|handsfree|siri|carplay|uplink|_UL|output_mono|output_multich|mic" /opt/speech/bin/natp.cfg 2>&1 | head -120
echo "== A2I / ioamp endpoints (output_* = capture)"
ls /dev 2>/dev/null | grep -iE "ioamp|a2i|amp|audio" | head -40
for d in /dev/ioamp* /dev/a2i* /dev/audio*; do [ -e "$d" ] && ls "$d" 2>/dev/null | head -60; done
ls /opt/sys/etc 2>/dev/null | grep -iE "amp|a2i|audio|router" | head
for f in /opt/sys/etc/*amp* /opt/sys/etc/*router* /opt/sys/etc/*a2i*; do
  [ -f "$f" ] && { echo "-- $f"; grep -oE "(input|output)_(mono|stereo|multich)[A-Za-z0-9_]*" "$f" | sort -u; }
done
strings /opt/sys/bin/dev-ioamp-router 2>/dev/null | grep -oE "(input|output)_(mono|stereo|multich)[A-Za-z0-9_]*" | sort -u | head -60
echo "== speech / hands-free processes"
pidin -f an 2>/dev/null | grep -iE "handsfree|natp|Speech|Connectivity|bt" | head -20
ls /opt/speech/bin 2>/dev/null | head -40
echo "== Bluetooth address candidates"
for f in $(find /var/opt/conn /opt/conn/etc -type f 2>/dev/null | head -400); do
  grep -oiE "([0-9a-f]{2}:){5}[0-9a-f]{2}" "$f" 2>/dev/null | sort -u | sed "s|^|$f: |" | head -5
done | head -40
sloginfo 2>/dev/null | grep -iE "local.{0,20}(bd_?addr|address)|own.{0,10}addr" | tail -10
EOS
echo
echo "saved: $OUT"
