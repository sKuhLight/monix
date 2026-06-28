#!/usr/bin/env bash
# Capture USB control traffic to/from the Audient iD24 while the Windows VM drives
# it. Shows a clear RECORDING banner + live packet count so you KNOW it's running.
set -e
OUT="${1:-/tmp/monix-cap1.pcapng}"

echo "Loading usbmon (sudo)…"
sudo modprobe usbmon
sudo chmod -R a+r /sys/kernel/debug/usb/usbmon 2>/dev/null || true

LINE=$(lsusb -d 2708:000d | head -1)
[ -n "$LINE" ] || { echo "iD24 not on USB (is it passed to the VM / plugged in?)"; exit 1; }
BUS=$(echo "$LINE" | sed -E 's/Bus 0*([0-9]+).*/\1/')
IFACE="usbmon$BUS"
echo
echo "================================================================"
echo "  ●  RECORDING on $IFACE  ->  $OUT"
echo "  Now click through every control in the Windows app, slowly."
echo "  The 'Packets' counter below MUST go up as you click."
echo "  Press Ctrl-C here when done."
echo "================================================================"
echo
# live packet counter on screen; writes full capture to file.
# (no 'exec' so we can fix ownership after you Ctrl-C)
sudo tshark -i "$IFACE" -w "$OUT" || true
sudo chown "$USER" "$OUT" 2>/dev/null || true
sudo chmod 644 "$OUT" 2>/dev/null || true
echo; echo "Saved capture to $OUT  (decode with: python3 capture/decode.py $OUT)"
