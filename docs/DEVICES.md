# Audient iD family — per-device reverse-engineering

Decoded from the official macOS app (`re/iD.x86_64`, classes `Id4`/`Id14`/`Id22`/
`Id24`/`Id44`/`id48`). Only the **iD24** is hardware-verified; everything else is
RE'd from the binary and marked accordingly. See `PROTOCOL.md` for the iD24 wire
details and the transport/entity model.

Confidence tags: **[HW]** verified on hardware (iD24 only) · **[bin]** decoded from a
binary constant / static table (high confidence) · **[inf]** inferred by analogy to
the iD24 (needs hardware confirmation).

## Summary

| Device | PID | Analogue in | Optical in (ports) | Optical out | Loopback→host | Mixer | Routing |
|--------|-----|------------|--------------------|-------------|---------------|-------|---------|
| iD4    | 0x03 | 2 | 0 | 0 | — | none | none |
| iD14   | 0x02 | 2 | 1 | 0 | — (−1) | 14 nodes | table (3 dest) |
| iD22   | 0x01 | 2 | 1 | 1 | — (−1) | 16 nodes | table (5 dest) |
| iD24 [HW] | 0x0d | 2 | 1 | 1 | **0x0a** | 16 nodes | formula |
| iD44   | 0x05 | 4 | 2 | 2 | — (−1) | 30 nodes | table (7 dest) |
| iD48   | 0x12 | 4 | 2 | 2 | **0x16** | 32 nodes | formula |

VID = `0x2708` for all. Counts are [bin] (from `<X>ProductDefinition::getNumberOf…`).
iD48 reports **4** analogue inputs in this app build (not 8) — flagged, verify on
hardware. "Optical in/out" counts ports; each port = 8 ADAT channels (or 2 S/PDIF).
Only iD24 and iD48 define a loopback-to-host output; the others inherit base −1.

## Mixer node layout (`defaultMixMap`)

`<X>RoutingDefinition::getDefaultInputForMixNode(node)` returns `defaultMixMap[node]`
— a flat per-node *source index*. The **DAW returns occupy the low source indices**,
then the hardware inputs (analogue mics, then optical/digital). The mixer crosspoint
cell = `node*6 + outChannel` ([HW] iD24; [inf] elsewhere).

- **iD14** — 14 nodes: `[4,5,6,7,8,9,10,11,12,13, 0,1,2,3]`
  sources `0–3 = DAW1–4`, `4–5 = Mic1–2`, `6–13 = Digi1–8`
  → strips `[Mic1, Mic2, Digi1..8, DAW1..4]`
- **iD22** — 16 nodes: `[14,15,16..23, 0,1,2,3,4,5]`
  sources `0–5 = DAW1–6`, `14–15 = Mic1–2`, `16–23 = Digi1–8`
  → `[Mic1, Mic2, Digi1..8, DAW1..6]`
- **iD24** [HW] — 16 nodes: `[16,17,18..25, 0,1,2,3,4,5]`
  sources `0–5 = DAW1–6`, `16–17 = Mic1–2`, `18–25 = Digi1–8`
  → `[Mic1, Mic2, Digi1..8, DAW1..6]`
- **iD44** — 30 nodes: `[24..43, 0..9]`
  nodes 0–19 = sources 24–43 (4 analogue + 16 optical + …), 20–29 = DAW 0–9 [inf]
- **iD48** — 32 nodes: `[24..47, 0..7]`
  nodes 0–23 = sources 24–47 (24 inputs), 24–31 = DAW 0–7 [inf]

`<X>ChannelLayoutDefinition::_channelDefinitions` (runtime-built vector, stride 40 B)
carries the UI strip labels + a channel-type enum (**0=Analogue, 1=Digital, 2=DAW,
4=Master, 5=Cue**); not a static array, so labels aren't dumpable without a live read.

## Routing

Output routing is one byte per physical output written to the routing entity,
**selector 0x06** ([HW] iD24). Two schemes:

### Formula devices (iD24, iD48): `getOutputSourceIndex(source, out)`
`out&1` = L/R within the output's stereo pair; switch on `source.type`.

**iD24** [HW] (verified live):
| type | code | destination |
|------|------|-------------|
| 0 | `out` | DAW direct (DAW Thru, full level) |
| 1 | `0x25 + (out&1)` | Main → 0x25/0x26 |
| 3, idx 0 | `0x1e + 2·idx + (out&1)` | Alt → 0x1e/0x1f |
| 3, idx 1 | ″ | Cue A → 0x20/0x21 |
| 3, idx 2 | ″ | Cue B → 0x22/0x23 |

**iD48** [bin] (untested) — same shape, **different constants**:
| type | code | note |
|------|------|------|
| 0 | `out` (`out−8` if out≥16) | DAW direct |
| 1 | `0x3f + (out&1)` (vtable-conditional; fallback `0x43`) | Main-ish bus |
| 2 | `0x41 + 2·idx + (out&1)` | bus bank |
| 3 | mono `idx + 0x3b`; stereo `0x32 + 2·idx + (out&1)` | Cue family |
| 4,5 | `0x43 + (out&1)` | default |
| 6,7 | `out − 8` | direct |

Bases differ markedly (0x3b/0x3f/0x41/0x43/0x32 vs iD24's 0x25/0x1e) — do **not**
reuse iD24 codes for the iD48.

### Table devices (iD14, iD22, iD44): `_routingDefinitions` + `getRouterSourceIndex`
`getRoutingDefinition(source)` searches a `std::vector<RoutingDefinition>` (stride 24
B: `name@0, type@8, index@12, code/flags@16`) — the **UI destination list**. The
actual wire index is computed by `getRouterSourceIndex(source, …)`, a jump-table
formula on `source.type` with per-type base offsets.

Destination lists ([bin], from the static constructors):
- **iD14** — 3: `MAIN MIX (1,0)`, `CUE MIX (3,0)`, `DAW THRU (0,0)`
- **iD22** — 5: `MAIN (1,0)`, `ALT SPK (2,0)`, `CUE A (3,0)`, `CUE B (3,1)`, `DAW THRU (0,0)`
  · router bases per type ≈ `0x1a / 0x21 / 0x23 / 0x25`
- **iD44** — 7: `MAIN (1,0)`, `ALT SPK (2,0)`, `CUE A–D (3,0..3)`, `DAW THRU (0,0)`
  · router bases ≈ `0x2e / 0x37 / 0x3b / 0x3d / 0x3f`

Source-type enum (output side): **0 = DAW Thru, 1 = Main, 2 = Alt, 3 = Cue** (index =
which cue). Default per-output routing lives in a `defaultOutputRouting {type,index}`
array (outputs 1+2 → Main, later pairs → Alt/Cue/DAW-Thru).

> ⚠️ Table-device wire codes are **not finalised**: the destination tables and router
> base offsets are decoded, but `getRouterSourceIndex`'s full per-type formula needs
> decoding **and** hardware confirmation before they can be trusted for writes.

## iD4 special case
No `Id4RoutingDefinition`, no `defaultMixMap`, no mixer — `build()` returns null. It
is a fixed 2-in/2-out interface: only input gain/phase and monitor controls apply.
Treat it as "no mixer / no routing".

## Open items (need hardware or more RE)
- **UAC entity IDs** (iD24: monitor 0x36, mixer 0x3c, routing 0x33, …) are assigned
  per USB descriptor and are *not* in the binary. For other devices they must be read
  from the device descriptors (the app discovers units by type at runtime). Until
  confirmed, non-iD24 profiles assume the iD24 entity IDs [inf].
- Table-device wire codes (`getRouterSourceIndex` full decode).
- iD44/iD48 analogue-vs-optical source split in `defaultMixMap`.
- iD48 analogue-input count (binary says 4).
