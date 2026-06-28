#!/usr/bin/env python3
# Decode captured USB control transfers into a readable control map.
# Disables Wireshark's usbaudio/usbhid dissectors so the raw setup (wValue/wIndex)
# decodes instead of being eaten by the class dissector.
# Usage: python3 decode.py capture.pcapng
import subprocess, sys, collections

if len(sys.argv) < 2:
    print("usage: decode.py capture.pcapng"); sys.exit(1)
pcap = sys.argv[1]

fields = ["frame.time_relative", "usb.bmRequestType", "usb.setup.bRequest",
          "usb.setup.wValue", "usb.setup.wIndex", "usb.setup.wLength", "usb.data_fragment"]
cmd = ["tshark", "-r", pcap, "--disable-protocol", "usbaudio",
       "--disable-protocol", "usbhid",
       "-Y", "usb.transfer_type==0x02 && usb.bmRequestType",
       "-T", "fields"] + sum([["-e", f] for f in fields], []) + ["-E", "separator=|"]
out = subprocess.run(cmd, capture_output=True, text=True).stdout

def hx(s):
    s = (s or "").strip()
    try: return int(s, 16) if s.startswith("0x") else (int(s) if s else 0)
    except ValueError: return 0

rows = []
for line in out.splitlines():
    p = (line.split("|") + [""] * 7)[:7]
    t, bmrt, breq, wval, widx, wlen = p[0], hx(p[1]), hx(p[2]), hx(p[3]), hx(p[4]), hx(p[5])
    data = (p[6] or "").replace(":", "").strip()
    if not bmrt:  # skip rows with no setup (the C halves)
        continue
    rows.append((t, bmrt, breq, wval, widx, wlen, data))

def fmt(bmrt, breq, wval, widx, wlen):
    d = "GET" if bmrt & 0x80 else "SET"
    return (f"{d} type=0x{bmrt:02x} req=0x{breq:02x} "
            f"wValue=0x{wval:04x}(sel=0x{wval>>8:02x},ch=0x{wval&0xff:02x}) "
            f"wIndex=0x{widx:04x}(entity=0x{widx>>8:02x}) len={wlen}")

print(f"# {len(rows)} control transfers with setup\n## TIME-ORDERED\n")
for t, bmrt, breq, wval, widx, wlen, data in rows:
    ds = f"  data={data}" if data else ""
    try: ts = f"{float(t):8.3f}"
    except ValueError: ts = t
    print(f"[{ts}] {fmt(bmrt,breq,wval,widx,wlen)}{ds}")

print("\n## DEDUPED (distinct request -> data values seen)\n")
seen = collections.OrderedDict()
for _, bmrt, breq, wval, widx, wlen, data in rows:
    e = seen.setdefault((bmrt, breq, wval, widx), [fmt(bmrt, breq, wval, widx, wlen), set()])
    if data: e[1].add(data)
for k, (rec, datas) in seen.items():
    ds = ("  data={" + ",".join(sorted(datas)) + "}") if datas else ""
    print(rec + ds)
