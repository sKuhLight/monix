# Monix — USB capture of the official app (to finish the protocol map)

Goal: run the **official Audient iD app on real Windows** (where the Thesycon
driver loads properly) with the iD24 **passed through** to the VM, and capture the
USB control traffic on the Linux host with `usbmon`. That gives the exact request
bytes for every control — routing, optical ADAT/SPDIF, loopback, clock, sample
rate, meters, and the state-sync block — with zero guessing.

Why it works: `quickemu`/qemu pass the USB device with `usb-host` (libusb on the
host), so all traffic still flows through the host's usbfs and `usbmon` sees it.

## 0. Install tools (one time)

```sh
paru -S --needed quickemu qemu-full wireshark-cli
sudo usermod -aG wireshark "$USER"     # capture without root (re-login after)
```

## 1. Get Windows + make the VM

```sh
cd ~/monix-vm            # or anywhere with ~12 GB free
quickget windows 11      # downloads official ISO + virtio drivers
quickemu --vm windows-11.conf   # boot, install Windows (skip MS account; no key needed)
```

After Windows is up, append USB passthrough to the generated conf:

```sh
echo 'usb_devices=("2708" "000d")' >> windows-11.conf
```
(or run `capture/passthrough.sh` which appends it). Reboot the VM with quickemu.

## 2. Install the Audient app in the guest

Copy the **Windows** iD installer (`iD-v4.4.2b6.exe`) into the VM and install it —
this time the Thesycon driver installs correctly (real Windows). Confirm the app
sees the iD24 and you can move faders/meters live in the guest.

## 3. Capture (on the Linux host)

```sh
bash capture/capture.sh          # auto-detects the iD24 bus, writes capture.pcapng
```
While it runs, in the Windows app **exercise every control deliberately, one at a
time, pausing between each** (so they're easy to tell apart in the trace):
move each fader, each pan, phase, pad/HPF, routing per channel, monitor knob,
dim/alt/talk/mono/mute, headphone, **sample rate change**, **optical ADAT↔SPDIF**,
**loopback on/off**, clock source. Then Ctrl-C the capture.

## 4. Decode

```sh
python3 capture/decode.py capture.pcapng
```
Prints every control transfer (bmRequestType/bRequest/wValue/wIndex/data) grouped
and de-duplicated → the complete control map to implement in `lib/`.
