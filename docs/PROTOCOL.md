# Audient iD USB control protocol — full reverse-engineering spec

## ★ CAPTURED CONTROL MAP (iD24, authoritative — from live USB capture)

Captured the official Windows app driving a real iD24 (USB passthrough + usbmon).
All control transfers use **bRequest = 0x01**; SET = `bmRequestType 0x21`,
GET = `0xA1`; `wValue = (selector<<8)|channel`, `wIndex = (entity<<8)`.
Values are little-endian.

| Function | entity | sel | channel | len | value |
|----------|--------|-----|---------|-----|-------|
| **Mixer crosspoint** (input→bus) | 0x3c | 0x01 | 0x00–0x5f (96 cells) | 2 | int16 gain |
| **Meters** (block read) | 0x3c | req **0x03** GET_MEM | offset 0..3 in wValue | 32/12/16/6 | level bytes |
| Input **phase** invert | 0x0b | 0x0d | 1..N | 1 | 0/1 |
| Input **gain/pad** (2-byte) | 0x0b | 0x0b | 1..N | 2 | int16 |
| **Headphone** volume | 0x0c | 0x02 | 3,4 (hp1) 5,6 (hp2) | 2 | int16 |
| **Routing** (output source) | 0x33 | 0x06 | output 0–5, 8–11 | 1 | source code (e.g. 0x1e/0x20/0x23/0x24/0x25) |
| Monitor **Mono** | 0x36 | 0x00 | 0 | 1 | 0/1 |
| Monitor **Mono mode** | 0x36 | 0x01 | 0 | 1 | 0=C,1=L,2=R |
| Monitor **Phase** | 0x36 | 0x03 | 0 | 1 | 0/1 |
| Monitor **Cut/Mute** | 0x36 | 0x04 | 0 | 1 | 0/1 |
| Monitor **Dim** | 0x36 | 0x05 | 0 | 1 | 0/1 |
| **Dim trim** level | 0x36 | 0x06 | 0 | 2 | int16 |
| **Talkback** on | 0x36 | 0x07 | 0 | 1 | 0/1 |
| **Talkback source** | 0x36 | 0x08 | 0 | 1 | 0x10=Mic1,0x11=Mic2,0x12=Digi1 |
| Monitor **Alt** | 0x36 | 0x0c | 0 | 1 | 0/1 |
| **Monitor volume** | 0x36 | 0x12 | 0 | 2 | int16 |
| Alt trim / extra | 0x36 | 0x15, 0x17 | 0 | 1/2 | (trim) |
| **Sample rate** | 0x29 | 0x01 | 0 | 4 | int32 Hz (44100/48000/88200/96000) |
| **System toggle A** (optical/clock/loopback) | 0x01 | 0x00 | 0 | 4 | 0/1 |
| **System toggle B** | 0x14 | 0x01 | 0 | 4 | 0/1 |
| Clock/sample-rate **read** | 0x3e | 0x06 | 0 | 4 | int32 (GET) |

Still to disambiguate by timestamp (optical-in vs optical-out vs clock-source vs
loopback among entities 0x01/0x14 and any digital-out-ADAT control). Capture file:
`/tmp/monix-cap1.pcapng` (decode with `capture/decode.py`).

---


Reverse-engineered from the official macOS app `iD.app` v4.4.2b6 (JUCE/C++ with
full symbols, Audient's `UacLib`), disassembled statically from the thin x86_64
slice (`re/iD.x86_64`) with `llvm-objdump`/`llvm-nm`, and verified live against an
iD24 through the `audient_id` kernel module.

### ★★ Authoritative routing + channel map (decoded from `Id24*` code)

**Device geometry** (`Id24ProductDefinition`): 2 analogue inputs, 1 optical input
(= up to 8 ADAT ch), 1 optical output, loopback-to-host on output **0x0a**.

**Mixer node order** (`defaultMixMap` @ `__const` + `createInputChannels`): the
mix has 16 nodes; the per-node default *source index* is
`[16,17, 18..25, 0,1,2,3,4,5]` →
`[Mic1, Mic2, Digi1..Digi8, DAW1, DAW2, DAW3, DAW4, DAW5, DAW6]`.
So source indices: **16=Mic1, 17=Mic2, 18..25=Digi1..8, 0..5=DAW1..6**.
Mixer crosspoint cell = `node*6 + bus*2 + chan` (bus 0=Main,1=CueA,2=CueB).
Input meter buffer (GET_MEM off 0) is contiguous in node order, 2 bytes/node:
`meter[node] = buf[node*2]` (`updateInputMeterValues`).

**Output routing source codes** — `Id24RoutingDefinition::getOutputSourceIndex(src,
out)` computes the code written to entity 0x33 sel 0x06 for physical output `out`:

| `RoutingSource` | code formula | result |
|---|---|---|
| type 0 (DAW direct) | `out` (identity) | output *i* ← DAW ch *i* — **DAW Thru**, full level |
| type 1 (Main) | `0x25 + (out&1)` | Main → **0x25 / 0x26** |
| type 3, idx 0 (Alt) | `0x1e + 2·idx + (out&1)` | Alt → **0x1e / 0x1f** |
| type 3, idx 1 (Cue A) | ″ | Cue A → **0x20 / 0x21** |
| type 3, idx 2 (Cue B) | ″ | Cue B → **0x22 / 0x23** |

(Earlier capture had Cue B as 0x23/0x24 — **off by one**; corrected to 0x22/0x23.)
`out&1` is the L/R position within the output's stereo pair.

**Mixer crosspoint addressing** (`UacMixerUnit::calculateMixerControlNumber`):
`controlNumber = in*stride + out`, stride = 6 (MainL,MainR,CueAL,CueAR,CueBL,CueBR).
Written via SET_CUR with selector 0x01, channel = controlNumber → matches our
`ctlSet(0x3c, 0x01, cell)`. A **stereo** strip must drive all four crosspoints of
its pair (L→busL, R→busR, and L→busR / R→busL **set to 0**); leaving the cross
cells at their old mono values is why a linked pair appeared to "only change left".

**Clock / S/PDIF** (ALSA controls, not ep0): `Audient Clock Selector Clock Source`
enum {0 = Internal, 1 = Optical1}; `Audient Optical1 Clock Validity` (CARD iface)
= on when a valid optical/S/PDIF clock is present. Feeding S/PDIF/ADAT into the
iD while Clock Source = Internal causes clock-drift **crackling** — slave to
Optical (index 1) when the optical input is in use.

Legend: ✅ decoded from code & verified on hardware · 🔶 decoded from code, value
table is data (confirm empirically) · 🔬 needs empirical confirmation.

---

## 1. Transport

All control goes over the device's **default control pipe (ep0)**. On Linux the
AudioControl interface is owned by `snd-usb-audio`; userspace can't send these
requests without detaching it (kills the card), so Monix relays them **in-kernel**
via the `audient_id` module (binds the unused DFU interface, no effect on audio).

A `UacLib::UsbRequest` is a 32-byte struct: `[0]=bmRequestType [1]=bRequest
[2..3]=wValue(LE) [4..5]=wIndex(LE) [8..]=data (juce::Array<uint8>)`.

`bmRequestType = (IN?0x80:0) | type | recipient`, where `type` = `0x20` class /
`0x40` vendor, `recipient` = `0x01` interface. `wIndex = (entityID<<8) | interface`
(interface 0). The entity ID is descriptor byte[3] (`bUnitID`) of the target unit.

## 2. Request families (✅ all decoded from code)

| family | dir | bmRequestType | bRequest | wValue | data len | source fn |
|--------|-----|---------------|----------|--------|----------|-----------|
| Feature-Unit GET_CUR | IN  | `0xA1` | `0x81` | `(selector<<8)|channel` | 1–2 | std UAC |
| Feature-Unit SET_CUR | OUT | `0x21` | `0x01` | `(selector<<8)|channel` | 1–2 | std UAC |
| Extension-Unit GET   | IN  | `0xA1` | `0x01` | `(a<<8)|b` | 4 | `UacUnitBase::sendGetRequest` |
| Extension-Unit SET   | OUT | `0x21` | `0x01` | `(a<<8)|b` | 4 | `UacUnitBase::sendSetRequest` |
| **MEM GET (meters/blocks)** | IN | `0xA1` | `0x03` | `offset` | N (block) | `UacUnitBase::sendGetMemRequest` |
| MEM SET              | OUT | `0x21` | `0x03` | `offset` | N | `sendSetMemRequest` |

- Extension-unit `(a,b)`: `a` = high byte (the unit's first param, often channel),
  `b` = low byte (control selector). All four Audient XU classes
  (`Input/Output/System/BlendMixer Control`) use **bRequest=1, length=4 (int32 LE)**.
- **MEM GET is how meters are read**: one request returns a whole byte array
  (buffer sized to the channel count) — see §5.

## 3. Feature Units (✅ verified live, from MixiD + model code)

`UacFeatureUnit::setPhaseInverter(ch,b)`, `setInputGain(ch,dB)` confirm phase/gain
are feature-unit writes.

| control | entity | selector | channel | len | SET | GET |
|---------|--------|----------|---------|-----|-----|-----|
| Monitor volume | 0x36 | 0x12 | 0 | 2 | ✅ | ✅ |
| Dim / Alt / Talk / Phase / Mono / Mute | 0x36 | 0x05/0x0c/0x07/0x03/0x00/0x04 | 0 | 1 | ✅ | ✅ |
| Headphone volume | 0x0a | 0x02 | 3,4 | 2 | ✅ | ❌ stalls |
| Channel mix volume | 0x3c | 0x01 | chan*6 (+1 = R) | 2 | ✅ | ❌ stalls |
| Channel phase | 0x0b | 0x0d | 1+chan | 1 | ✅ | ❌ stalls |
| Routing (write) | 0x33 | 0x06 | chan | 1 | ✅ | ❌ |

Channel/phones/phase mix controls are **write-only in firmware** (GET_CUR stalls).

## 4. Extension Units present on iD24 (from descriptors)

| bUnitID | wExtCode | in-pins | role (inferred) |
|---------|----------|---------|-----------------|
| 62 | 0x0000 | 1 | clock / sample-rate (GET=48000) ✅ |
| 52 | 0x0000 | 5 | state / meter block 🔶 |
| 51 | 0x0000 | 5 | (write path) |
| 50 | 0x0000 | 2 | |
| 54 | 0x0001 | 1 | |
| 55 | 0x0002 | 1 | |
| 60 | (mixer)  | 1 | MIXER_UNIT |
| 10,11,12 | (feature) | | FEATURE_UNITs |

`AudientInterface` resolves XU roles via `getExtensionUnitWithCode(code, unitID)`
(matches both ext-code and unit-id) and `getUnitWithId(id)` — the actual IDs come
from the per-model **ProductDefinition** data structure (not code immediates), so
the role→entity table is confirmed empirically (§9).

## 5. Meters (✅ format decoded — `MixerModel::updateInputMeterValues`)

```
buf.resize(numInputChannels);
unit->sendGetMemRequest(offset, buf, numInputChannels);   // ONE transfer
for each channel: setMeterValue( scale(buf[channel]) );    // 1 byte per channel
```
→ `GET_MEM`: `bmRequestType=0xA1 bRequest=0x03 wValue=offset wIndex=(meterEntity<<8)`,
read `numChannels` bytes; **each byte is one channel's level**. Poll the block at
~15–30 Hz (one transfer) — do NOT read per-channel (that floods ep0 and the kernel
resets the device). Meter entity + offset + byte→dB scaling: confirm in §9.

## 6. StateSynchroniser (`UacStateSynchroniser`)

Built in the interface ctor from **one** extension unit (from
`getExtensionUnitWithCode`). `update()` reads it and fires
`addStateChangesHandler(entity, range, ?, fn(UacUnitControlId))` callbacks; models
cache values (their `getParameter` is a cache read, not a live USB read). Driven by
`UacLib::PollingTimer`. This is how the macOS app shows live state including the
write-only controls — it reads them through this XU rather than the feature units.

## 7. Value scaling

- Feature-unit volume: int16, Monix maps `0..1` ↔ `0x8000..0xFFFF`
  (`floatToU16`/`u16ToFloat`) — round-trips correctly for monitor volume. 🔶 dB
  curve for faders TBD.
- XU/meter values: int32/byte; scaling (linear vs dB) per control — 🔬.

## 8. Clock / sample rate (✅)

`GET` extension unit 62 → int32 = current sample rate in Hz (read 48000).

## 9. Empirical fill-in (formats known, data pending)

With §2 formats fixed, the remaining unknowns are pure data, fastest confirmed live:
1. Meter entity + offset + scaling — `GET_MEM` probe (one block read; watch it move
   with input signal).
2. The StateSynchroniser XU (which entity returns the full state block) and its
   layout → enables reading back faders/phase/routing.
3. Per-control selectors & dB scaling for the model params.
