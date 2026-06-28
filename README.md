# Monix

[![CI](https://github.com/sKuhLight/monix/actions/workflows/ci.yml/badge.svg)](https://github.com/sKuhLight/monix/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A standalone, native Linux control application **and** driver for Audient iD-series
USB audio interfaces. Successor to the MixiD experiments — built from scratch around
a properly reverse-engineered protocol so it can do what the official app does:
live state sync, channel faders, routing, and VU metering, **while audio keeps
playing**.

## Compatibility

- **Any modern Linux distro** — Arch/CachyOS, Debian 12+, Ubuntu 22.04+, Fedora,
  etc. The module Makefile auto-detects Clang- vs GCC-built kernels, so CachyOS's
  Clang kernel and the GCC kernels on Debian/Fedora both build cleanly.
- **Kernel ≥ 5.9** — the module uses the in-kernel `usb_control_msg_send/recv`
  helpers (added in 5.9), so control + audio can coexist without claiming the
  audio interface.
- **PipeWire** is recommended (for the Windows-style virtual-device split); the
  rest works on any ALSA setup.
- Hardware-verified on the **iD24**; other iD models are detected and supported on
  a best-effort basis (see [Device support](#device-support)).

## Why this exists

On Linux the iD control protocol is sent to a USB interface that `snd-usb-audio`
owns, and userspace can't send control requests to it without detaching the audio
driver (which kills the sound card). We solve this the same way the kernel does for
other pro interfaces: a small companion kernel module relays control transfers from
inside the kernel, so control and audio run at the same time with no glitches.

## Layout

```
driver/   companion kernel module (relays USB control transfers; reads via ioctl)
lib/      userspace protocol library (control + state + meters)  [in progress]
app/      the GUI control panel (Audient-like layout, custom theme)  [in progress]
docs/     PROTOCOL.md — reverse-engineering notes / protocol spec
re/       reverse-engineering working files (macOS app slice, scripts) [gitignored]
```

## Requirements

A C++17 toolchain, CMake ≥ 3.16, git, your **kernel headers**, and dev packages
for **ALSA**, **GLFW**, and **OpenGL**. Dear ImGui is fetched automatically by
CMake (first build needs internet). DKMS is optional but recommended — it rebuilds
the module automatically on kernel updates.

```sh
# Arch / CachyOS / Manjaro  (use headers matching your kernel: linux-cachyos-headers, …)
sudo pacman -S --needed base-devel cmake git linux-headers alsa-lib glfw mesa dkms

# Debian / Ubuntu / Pop!_OS
sudo apt install build-essential cmake git linux-headers-$(uname -r) \
                 libasound2-dev libglfw3-dev libgl1-mesa-dev dkms \
                 libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev

# Fedora
sudo dnf install gcc-c++ cmake git kernel-devel alsa-lib-devel glfw-devel \
                 mesa-libGL-devel dkms
```

## Install

```sh
git clone <repo-url> monix && cd monix
```

### 1) Kernel module (companion driver, not a custom kernel)

A small out-of-tree module that relays control transfers from inside the kernel so
the GUI works while audio plays. It declares the device, so once installed it
**auto-loads when you plug the interface in** — no boot-time setup.

**Recommended — DKMS** (survives kernel upgrades):
```sh
sudo cp -r driver /usr/src/audient_id-0.1
sudo dkms add     -m audient_id -v 0.1
sudo dkms install -m audient_id -v 0.1
sudo cp driver/99-audient-id.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules
```

**Or — plain make** (rebuild yourself after each kernel update):
```sh
cd driver && make && sudo make install && cd ..   # builds + installs module + udev rule
sudo modprobe audient_id                          # load now (or just replug the device)
```

Then add yourself to the `audio` group (the udev rule grants it access to
`/dev/audient_id*`) and re-login:
```sh
sudo usermod -aG audio "$USER"
```

### 2) App + CLI

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/app/monix-gui     # the GUI control panel
./build/monixctl status   # CLI: dump live device state
```

Sanity check: `ls /dev/audient_id*` should list a node and `monixctl status` should
print the sample rate + monitor state.

## Device support

The library is **profile-driven**: on connect it detects the USB PID and loads a
per-device profile (counts, mixer node layout, routing scheme) reverse-engineered
from the macOS app — see [`docs/DEVICES.md`](docs/DEVICES.md). The GUI builds its
channel strips and routing matrix from that profile, so the mixer adapts to each
model. Unknown PIDs fall back to the iD24 layout.

| Model | Detect | Mixer/faders | Output routing | Tested |
|-------|:------:|:------------:|:--------------:|:------:|
| iD24  | ✅ | ✅ | ✅ formula | **hardware-verified** |
| iD48  | ✅ | ✅ | ◐ formula (RE'd, untested) | binary only |
| iD14 / iD22 / iD44 | ✅ | ✅ | ◐ table scheme not yet wired | binary only |
| iD4   | ✅ | — (no mixer) | — | binary only |

Non-iD24 models show an **"experimental"** banner; their routing UI is read-only
until the table-scheme wire codes are decoded and someone with the hardware can
confirm. Mixer faders/meters use the shared scheme and should work, but are
unverified off the iD24.

## Status

- ✅ Kernel module — control + meter reads while audio plays (no card teardown).
- ✅ Protocol library — routing, mixer crosspoints, sample rate, clock source,
  optical (ADAT/S-PDIF) mode, monitor/master toggles, talkback, phones; control
  map decoded from the macOS app and verified on hardware.
- ✅ GUI — dark-violet, Audient-style: channel strips with stereo/mono link,
  fader/pan/phase/meter, Master/Cue A/B mixes, monitor section, and a System Panel
  (routing matrix incl. digital outs, sample rate, clock source with S-PDIF lock
  warning, digital I/O mode, trims).

## Multiple devices & gaming (Windows-style split)

The iD24 is class-compliant, so Linux sees it as **one** 16-ch output + 12-ch
input node with an all-`aux` channel map. Games and surround-aware apps then
spread or mis-map stereo across channels your monitors don't carry — audio
"bugs out". (Windows avoids this only because Audient's driver splits the device
into named stereo endpoints.)

**Easiest: do it in the app.** Open **VIRTUAL DEVICES** in Monix — tick the stereo
sinks/sources you want (they appear instantly, no PipeWire restart), hit *Enable
inputs* for the mics/ADAT, and *Save as startup config* to make them persist.

`setup/` has the same thing as hand-editable PipeWire configs — stereo
sinks/sources each wired to one channel pair, while the full multichannel node
stays for DAWs:

- `setup/id24-stereo-sink.conf` — minimal: one clean "iD24 Stereo" sink on DAW 1+2.
  Point games/desktop at it. Fixes the gaming issue with the least fuss.
- `setup/id24-split.conf` — full Windows-style set: Main 1+2 / Out 3+4 / Phones 5+6
  sinks, and Mic 1+2 / ADAT / Loopback sources.

```sh
mkdir -p ~/.config/pipewire/pipewire.conf.d
cp setup/id24-split.conf ~/.config/pipewire/pipewire.conf.d/   # or id24-stereo-sink.conf
systemctl --user restart pipewire pipewire-pulse
```

For inputs, set the card to its **Duplex** profile once (pavucontrol →
"Mehrkanal-Duplex"); WirePlumber remembers it. Then pick the stereo device you
want as default in your sound settings.

## Troubleshooting

- **No `/dev/audient_id*`** — module didn't load. Check `lsmod | grep audient` and
  `dmesg | grep audient_id`; ensure kernel headers are installed and (Arch) match
  the running kernel.
- **GUI says "no device" / permission denied** — not in the `audio` group yet
  (re-login after `usermod`), or the udev rule isn't installed.
- **S/PDIF crackling** — set Clock source to **Optical** in the System Panel so the
  iD slaves to the incoming stream, and use the same sample rate at both ends.

## Credits / lineage

Protocol reverse-engineered from the official macOS `iD.app`. Earlier groundwork
(the SET control values, device list) came from the MixiD project.
