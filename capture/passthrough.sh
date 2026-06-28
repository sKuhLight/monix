#!/usr/bin/env bash
# Append iD24 USB passthrough to a quickemu VM config (run once, VM powered off).
#   bash passthrough.sh ~/monix-vm/windows-11.conf
set -e
CONF="${1:?usage: passthrough.sh <quickemu .conf>}"
if grep -q 'usb_devices' "$CONF"; then echo "usb_devices already present in $CONF"; exit 0; fi
echo 'usb_devices=("2708:000d")' >> "$CONF"
echo "Added iD24 (2708:000d) passthrough to $CONF. Start the VM with quickemu."
echo "Note: while the VM owns the device, host audio on the iD24 is unavailable."
