# Monix

A standalone, native Linux control application **and** driver for Audient iD-series
USB audio interfaces. Successor to the MixiD experiments — built from scratch around
a properly reverse-engineered protocol so it can do what the official app does:
live state sync, channel faders, routing, and VU metering, **while audio keeps
playing**.

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

## Build & run

```sh
# 1) kernel module (once)
cd driver && make && sudo make install && sudo modprobe audient_id && cd ..
#    ensure your user is in the 'audio' group (the udev rule grants access)

# 2) library + CLI + GUI
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/monixctl status      # CLI: dump live device state
./build/app/monix-gui        # the GUI control panel
```

## Status

- ✅ Driver (kernel module) — control + read while audio plays.
- ✅ Protocol library (`lib/`) — verified live: monitor volume, master toggles,
  sample rate, routing/levels read back; all controls settable.
- ✅ GUI (`app/`) — dark-violet, Audient-style: input strips (fader/phase/meter),
  monitor section (main/phones + Dim/Alt/Talk/Ø/Mono/Mute), live polling.
- 🔬 In progress: input-meter scaling (entity 52), routing UI, full fader
  readback via the StateSynchroniser block read.

## Credits / lineage

Protocol reverse-engineered from the official macOS `iD.app`. Earlier groundwork
(the SET control values, device list) came from the MixiD project.
