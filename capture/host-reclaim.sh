#!/usr/bin/env bash
# Give the iD24 back to the host after the VM is shut down (re-enumerate so all
# host drivers rebind). Equivalent to a replug, no cable needed.
set -e
DEV=""
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  if [ "$(cat "$d/idVendor")" = "2708" ] && [ "$(cat "$d/idProduct" 2>/dev/null)" = "000d" ]; then
    DEV=$(basename "$d"); break
  fi
done
[ -n "$DEV" ] || { echo "iD24 not found (still owned by VM? shut the VM down first)"; exit 1; }
echo "re-enumerating $DEV ..."
echo 0 | sudo tee /sys/bus/usb/devices/$DEV/authorized >/dev/null
sleep 1
echo 1 | sudo tee /sys/bus/usb/devices/$DEV/authorized >/dev/null
echo "done — host drivers should rebind (check: lsusb -d 2708:000d; lsmod | grep audient_id)"
