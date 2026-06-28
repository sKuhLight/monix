#!/usr/bin/env bash
# Free the iD24 from all host drivers (snd-usb-audio, usbhid, audient_id) so qemu
# can pass it through to the VM. Run this BEFORE launching the VM.
set -e
# locate the iD24 usb device node (e.g. 5-4)
DEV=""
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  if [ "$(cat "$d/idVendor")" = "2708" ] && [ "$(cat "$d/idProduct" 2>/dev/null)" = "000d" ]; then
    DEV=$(basename "$d"); break
  fi
done
[ -n "$DEV" ] || { echo "iD24 not found"; exit 1; }
echo "iD24 at $DEV"

# stop PipeWire from using it: move default sink away if it's the iD24
if command -v wpctl >/dev/null && wpctl status 2>/dev/null | grep -qi 'iD24.*\*'; then
  echo "note: iD24 is your default audio sink — set another output in your sound settings first"
fi

# unbind every interface from its host driver
for intf in /sys/bus/usb/devices/$DEV:*; do
  [ -e "$intf/driver" ] || continue
  i=$(basename "$intf"); drv=$(basename "$(readlink "$intf/driver")")
  echo "  unbinding $i from $drv"
  echo -n "$i" | sudo tee "/sys/bus/usb/devices/$intf/driver/unbind" >/dev/null 2>&1 \
    || echo -n "$i" | sudo tee "/sys/bus/usb/drivers/$drv/unbind" >/dev/null
done
echo "iD24 released from host. Launch the VM now:  cd ~ && quickemu --vm windows-11.conf"
