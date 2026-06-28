#!/usr/bin/env bash
# Guided, labeled capture — removes all guesswork. Starts a USB capture, then
# walks you through each action; press Enter the MOMENT BEFORE you do each one.
# Each step's USB requests are timestamped so decode_guided.py can label them exactly.
#
# Run with the device passed to the VM and the official iD app open.
set -e
OUT=/tmp/monix-cap1.pcapng
LOG=/tmp/monix-steps.log
: > "$LOG"

echo "Loading usbmon (sudo)…"; sudo modprobe usbmon
BUS=$(lsusb -d 2708:000d | sed -E 's/Bus 0*([0-9]+).*/\1/')
[ -n "$BUS" ] || { echo "iD24 not on USB bus (passed to VM?)"; exit 1; }

echo "Starting capture on usbmon$BUS …"
sudo tshark -i "usbmon$BUS" -w "$OUT" -q >/dev/null 2>&1 &
TPID=$!
sleep 2   # let capture warm up

step() {  # $1 = label
  read -r -p ">>> NEXT: $1
      (press Enter, THEN do it slowly, then continue to the next) "
  echo "$(date +%s.%N) | $1" >> "$LOG"
}

echo "============================================================"
echo " In the official iD app + System Panel, do each step when prompted."
echo " Press Enter, then perform ONLY that action, then read the next."
echo "============================================================"

# --- routing of output 1+2 through each destination ---
step "ROUTE 1+2 -> Main Mix"
step "ROUTE 1+2 -> Alt Spk"
step "ROUTE 1+2 -> Cue A"
step "ROUTE 1+2 -> Cue B"
step "ROUTE 1+2 -> DAW Thru"
step "ROUTE 1+2 -> Main Mix (restore)"
# --- phones routing (for the cue workflow) ---
step "ROUTE Phones 5+6 -> Cue A"
# --- channel identity: move ONE fader at a time on the MAIN mix ---
step "MIXER select MAIN MIX, then move MIC 1 fader fully down then back up"
step "MIXER move MIC 2 fader fully down then back up"
step "MIXER move DAW 1+2 fader fully down then back up"
# --- cue send: build Cue A ---
step "MIXER select CUE A, then move DAW 1+2 fader down then up"
# --- pan ---
step "MIXER move MIC 1 PAN fully Left, then fully Right, then center"
# --- digital input mode ---
step "SYSTEM set Digital In -> S/PDIF"
step "SYSTEM set Digital In -> ADAT"
read -r -p ">>> DONE — press Enter to stop the capture "

sudo kill "$TPID" 2>/dev/null || true
sleep 1
sudo chown "$USER" "$OUT" 2>/dev/null || true
echo "Saved: $OUT  +  steps: $LOG"
echo "Now run: python3 capture/decode_guided.py"
