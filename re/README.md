# Reverse-engineering tools

Helpers used to decode the Audient control protocol from the official macOS app.
The app binary itself is **not** redistributed here — supply your own.

## Getting the binary
From the official `iD vX.Y.Z.dmg`, extract the app and take the x86_64 slice:
```sh
# mount the dmg, copy iD.app out, then:
llvm-lipo "iD.app/Contents/MacOS/iD" -thin x86_64 -output re/iD.x86_64
```
(The Mach-O has full JUCE/`UacLib`/`Id*` symbols, which is what makes this tractable.)

## Tools
- `./dis.sh <symbol-substring> [maxlines]` — disassemble the first symbol whose name
  contains the substring (Intel syntax). e.g. `./dis.sh Id24RoutingDefinition20getOutputSourceIndex`
- `python3 dumpconst.py <hexaddr> <count> [int32|int8]` — dump a static array at a
  virtual address from `__const`/`__data` (e.g. the `defaultMixMap` tables).

Requires `llvm-objdump` / `llvm-nm` on PATH.

## Findings
Decoded protocol is written up in [`../docs/PROTOCOL.md`](../docs/PROTOCOL.md)
(iD24, wire-level + verified) and [`../docs/DEVICES.md`](../docs/DEVICES.md)
(per-device: iD4/14/22/24/44/48).
