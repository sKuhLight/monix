#include "monix.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <string>
#include <fstream>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <alsa/asoundlib.h>

namespace monix {
namespace {
struct idctl_msg {
    uint8_t  bRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
    uint8_t  data[64];
} __attribute__((packed));
#define IDCTL_IOC_XFER _IOWR(0xA1, 1, struct idctl_msg)

// entities / selectors (from docs/PROTOCOL.md captured map)
constexpr uint8_t E_MON = 0x36, E_SR = 0x29, E_SRGET = 0x3e, E_HP = 0x0c,
                  E_IN = 0x0b, E_MIX = 0x3c, E_ROUTE = 0x33;

uint8_t masterSel(Master m) {
    switch (m) {
        case Master::Mono:     return 0x00;
        case Master::Phase:    return 0x03;
        case Master::Cut:      return 0x04;
        case Master::Dim:      return 0x05;
        case Master::Talkback: return 0x07;
        case Master::Alt:      return 0x0c;
        case Master::MonoMode: return 0xff; // use setMonoMode()
    }
    return 0xff;
}

// Per-device profiles (see docs/DEVICES.md). Fields: name, pid, micInputs,
// digitalInputs, dawReturns, outputs, loopbackOut(-1=none), hasMixer, hasRouting,
// scheme, verified. MKII variants reuse their base sibling's layout (best guess).
const DeviceInfo kDevices[] = {
    {"iD4",       0x0003, 2,  0,  0, 2, -1,   false, false, RoutingScheme::None,    false},
    {"iD4 MKII",  0x0009, 2,  0,  0, 2, -1,   false, false, RoutingScheme::None,    false},
    {"iD14",      0x0002, 2,  8,  4, 4, -1,   true,  true,  RoutingScheme::Table,   false},
    {"iD14 MKII", 0x0008, 2,  8,  4, 4, -1,   true,  true,  RoutingScheme::Table,   false},
    {"iD22",      0x0001, 2,  8,  6, 8, -1,   true,  true,  RoutingScheme::Table,   false},
    {"iD24",      0x000d, 2,  8,  6, 6, 0x0a, true,  true,  RoutingScheme::Formula, true },
    {"iD44",      0x0005, 4, 16, 10, 8, -1,   true,  true,  RoutingScheme::Table,   false},
    {"iD44 MKII", 0x000b, 4, 16, 10, 8, -1,   true,  true,  RoutingScheme::Table,   false},
    {"iD48",      0x0012, 4, 20,  8, 8, 0x16, true,  true,  RoutingScheme::Formula, false},
};
} // namespace

const DeviceInfo* findDevice(uint16_t pid) {
    for (auto& d : kDevices) if (d.productId == pid) return &d;
    return nullptr;
}

// Scan sysfs for a connected Audient device and return its USB product id (0 if
// none found). Lets Device pick the right profile without the caller knowing it.
uint16_t connectedProductId() {
    DIR* d = opendir("/sys/bus/usb/devices"); if (!d) return 0;
    uint16_t pid = 0;
    for (dirent* e; (e = readdir(d)); ) {
        std::string b = std::string("/sys/bus/usb/devices/") + e->d_name;
        std::ifstream vf(b + "/idVendor"); std::string v;
        if (!(vf >> v) || v != "2708") continue;
        std::ifstream pf(b + "/idProduct"); std::string p;
        if (pf >> p) { pid = (uint16_t)strtol(p.c_str(), nullptr, 16); break; }
    }
    closedir(d); return pid;
}

// int16, 1/256 dB: 0x0000 = 0 dB (max), 0x8000 = mute. Map 0..1 -> [-60 dB .. 0 dB].
int16_t floatToVol(float v) {
    if (v <= 0.0f) return (int16_t)0x8000;
    if (v > 1.0f) v = 1.0f;
    float db = -60.0f * (1.0f - v);
    int r = (int)lroundf(db * 256.0f);
    if (r < -32768) r = -32768; if (r > 32767) r = 32767;
    return (int16_t)r;
}
float volToFloat(int16_t v) {
    if (v <= (int16_t)0x8000) return 0.0f;
    float db = v / 256.0f;
    float f = 1.0f + db / 60.0f;
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

Device::~Device() { close(); }
bool Device::open(const char* path) {
    char buf[32];
    for (int n = 0; n < 8 && fd_ < 0; n++) {
        const char* p = path;
        if (!p) { snprintf(buf, sizeof buf, "/dev/audient_id%d", n); p = buf; }
        fd_ = ::open(p, O_RDWR | O_CLOEXEC);
        if (path) break;
    }
    if (fd_ < 0) { err_ = "cannot open /dev/audient_id* (kernel module loaded?)"; return false; }
    // Pick the device profile from the connected USB PID; fall back to the iD24
    // (our verified reference) if the PID is unknown so the app still functions.
    uint16_t pid = connectedProductId();
    info_ = findDevice(pid);
    if (!info_) info_ = findDevice(0x000d);   // iD24 fallback
    return true;
}
void Device::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

bool Device::xfer(uint8_t rt, uint8_t req, uint16_t wValue, uint16_t wIndex,
                  void* data, uint8_t len, bool in) {
    if (fd_ < 0) { err_ = "device not open"; return false; }
    idctl_msg m; memset(&m, 0, sizeof m);
    m.bRequestType = rt; m.bRequest = req; m.wValue = wValue; m.wIndex = wIndex; m.wLength = len;
    if (!in && data && len) memcpy(m.data, data, len);
    if (ioctl(fd_, IDCTL_IOC_XFER, &m) != 0) { err_ = strerror(errno); return false; }
    if (in && data && len) memcpy(data, m.data, len);
    return true;
}

bool Device::ctlSet(uint8_t e, uint8_t s, uint8_t ch, const void* d, uint8_t len) {
    return xfer(0x21, 0x01, (uint16_t)((s << 8) | ch), (uint16_t)(e << 8), (void*)d, len, false);
}
bool Device::ctlGet(uint8_t e, uint8_t s, uint8_t ch, void* d, uint8_t len) {
    return xfer(0xA1, 0x01, (uint16_t)((s << 8) | ch), (uint16_t)(e << 8), d, len, true);
}
bool Device::memGet(uint8_t e, uint16_t off, void* d, uint8_t len) {
    return xfer(0xA1, 0x03, off, (uint16_t)(e << 8), d, len, true);
}

// ---- monitor section ----
bool Device::setMaster(Master m, bool on) {
    uint8_t s = masterSel(m); if (s == 0xff) return false;
    uint8_t b = on ? 1 : 0; return ctlSet(E_MON, s, 0, &b, 1);
}
bool Device::getMaster(Master m, bool& out) {
    uint8_t s = masterSel(m); if (s == 0xff) return false;
    uint8_t b = 0; if (!ctlGet(E_MON, s, 0, &b, 1)) return false; out = b != 0; return true;
}
bool Device::setMonoMode(MonoMode m) { uint8_t v = (uint8_t)m; return ctlSet(E_MON, 0x01, 0, &v, 1); }
bool Device::setMonitorVolume(float v) { int16_t x = floatToVol(v); return ctlSet(E_MON, 0x12, 0, &x, 2); }
bool Device::getMonitorVolume(float& out) {
    int16_t x = 0; if (!ctlGet(E_MON, 0x12, 0, &x, 2)) return false; out = volToFloat(x); return true;
}
bool Device::setDimTrim(float v) { int16_t x = floatToVol(v); return ctlSet(E_MON, 0x06, 0, &x, 2); }
bool Device::setAltTrim(float v) { int16_t x = floatToVol(v); return ctlSet(E_MON, 0x17, 0, &x, 2); }
bool Device::setTalkbackSource(TalkbackSource s) { uint8_t c = (uint8_t)s; return ctlSet(E_MON, 0x08, 0, &c, 1); }

// ---- digital optical I/O mode (S/PDIF vs ADAT) ----
// OpticalMode wire value (RE'd from serialiseSystemSettings): 0 = ADAT, 1 = S/PDIF.
bool Device::setDigitalInputADAT(bool adat)  { int32_t v = adat?0:1; return ctlSet(0x01, 0x00, 0, &v, 4); }
bool Device::setDigitalOutputADAT(bool adat) { int32_t v = adat?0:1; return ctlSet(0x14, 0x01, 0, &v, 4); }
bool Device::getDigitalInputADAT(bool& adat)  { int32_t v=-1; if (!ctlGet(0x01,0x00,0,&v,4)) return false; adat = (v==0); return true; }
bool Device::getDigitalOutputADAT(bool& adat) { int32_t v=-1; if (!ctlGet(0x14,0x01,0,&v,4)) return false; adat = (v==0); return true; }

// ---- audible monitor volume via ALSA ("Speaker Playback Volume") ----
namespace {
int findAudientCard() {
    int c = -1;
    while (snd_card_next(&c) == 0 && c >= 0) {
        char* name = nullptr;
        if (snd_card_get_name(c, &name) == 0 && name) {
            bool hit = strstr(name, "iD") || strstr(name, "Audient");
            free(name);
            if (hit) return c;
        }
    }
    return -1;
}
snd_mixer_elem_t* openSpeaker(snd_mixer_t** h) {
    int card = findAudientCard(); if (card < 0) return nullptr;
    char hw[16]; snprintf(hw, sizeof hw, "hw:%d", card);
    if (snd_mixer_open(h, 0) < 0) return nullptr;
    if (snd_mixer_attach(*h, hw) < 0 || snd_mixer_selem_register(*h, nullptr, nullptr) < 0
        || snd_mixer_load(*h) < 0) { snd_mixer_close(*h); return nullptr; }
    snd_mixer_selem_id_t* sid; snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Speaker");
    snd_mixer_elem_t* e = snd_mixer_find_selem(*h, sid);
    if (!e) { snd_mixer_close(*h); return nullptr; }
    return e;
}
}
bool Device::setSpeakerVolume(float v) {
    if (v < 0) v = 0; if (v > 1) v = 1;
    snd_mixer_t* h = nullptr; snd_mixer_elem_t* e = openSpeaker(&h);
    if (!e) { err_ = "ALSA Speaker control not found"; return false; }
    long lo, hi; snd_mixer_selem_get_playback_volume_range(e, &lo, &hi);
    long val = lo + (long)lroundf((hi - lo) * v);
    int rc = snd_mixer_selem_set_playback_volume_all(e, val);   // all channels = balanced stereo
    snd_mixer_close(h);
    return rc == 0;
}
bool Device::getSpeakerVolume(float& out) {
    snd_mixer_t* h = nullptr; snd_mixer_elem_t* e = openSpeaker(&h);
    if (!e) return false;
    long lo, hi, val = 0; snd_mixer_selem_get_playback_volume_range(e, &lo, &hi);
    snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &val);
    snd_mixer_close(h);
    out = (hi > lo) ? (float)(val - lo) / (hi - lo) : 0.0f;
    return true;
}
bool Device::setClockSource(int index) {
    int card = findAudientCard(); if (card < 0) { err_ = "card not found"; return false; }
    char hw[16]; snprintf(hw, sizeof hw, "hw:%d", card);
    snd_ctl_t* ctl; if (snd_ctl_open(&ctl, hw, 0) < 0) return false;
    snd_ctl_elem_id_t* id; snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, "Audient Clock Selector Clock Source");
    snd_ctl_elem_value_t* val; snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    snd_ctl_elem_value_set_enumerated(val, 0, (unsigned)index);
    int rc = snd_ctl_elem_write(ctl, val);
    snd_ctl_close(ctl);
    return rc >= 0;
}
// Read an ALSA control's first value as a long (enum index or 0/1 bool). -1 on fail.
static long readCtlValue(int card, const char* name) {
    if (card < 0) return -1;
    char hw[16]; snprintf(hw, sizeof hw, "hw:%d", card);
    snd_ctl_t* ctl; if (snd_ctl_open(&ctl, hw, 0) < 0) return -1;
    snd_ctl_elem_id_t* id; snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_info_t* inf; snd_ctl_elem_info_alloca(&inf);
    snd_ctl_elem_info_set_id(inf, id);
    if (snd_ctl_elem_info(ctl, inf) < 0) {   // try CARD iface (validity flags live there)
        snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_CARD);
        snd_ctl_elem_info_set_id(inf, id);
        if (snd_ctl_elem_info(ctl, inf) < 0) { snd_ctl_close(ctl); return -1; }
    }
    snd_ctl_elem_value_t* val; snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    long out = -1;
    if (snd_ctl_elem_read(ctl, val) >= 0) {
        auto t = snd_ctl_elem_info_get_type(inf);
        out = (t == SND_CTL_ELEM_TYPE_ENUMERATED) ? snd_ctl_elem_value_get_enumerated(val, 0)
            : (t == SND_CTL_ELEM_TYPE_BOOLEAN)    ? snd_ctl_elem_value_get_boolean(val, 0)
                                                  : snd_ctl_elem_value_get_integer(val, 0);
    }
    snd_ctl_close(ctl);
    return out;
}
int  Device::getClockSource()    { return (int)readCtlValue(findAudientCard(), "Audient Clock Selector Clock Source"); }
bool Device::getOpticalClockValid() { return readCtlValue(findAudientCard(), "Audient Optical1 Clock Validity") > 0; }

// ---- sample rate / clock ----
bool Device::setSampleRate(int hz) { int32_t v = hz; return ctlSet(E_SR, 0x01, 0, &v, 4); }
// Read back from the same entity we set (0x29/0x01); the 0x3e/0x06 "clock" GET
// returns a status word (0xff0000ff), not the rate. Verified live: 0x29 → 0xbb80.
int  Device::getSampleRate() { int32_t v = 0; return ctlGet(E_SR, 0x01, 0, &v, 4) ? v : -1; }

// ---- headphones ----
bool Device::setHeadphoneVolume(int hp, float v) {
    int16_t x = floatToVol(v); uint8_t l = (uint8_t)(3 + hp * 2), r = (uint8_t)(4 + hp * 2);
    return ctlSet(E_HP, 0x02, l, &x, 2) && ctlSet(E_HP, 0x02, r, &x, 2);
}

// Mix-bus master (FU 0x0c sel 0x02): Main ch 1/2, Cue A 3/4, Cue B 5/6.
bool Device::setBusVolume(Bus bus, float v) {
    int16_t x = floatToVol(v);
    uint8_t l = (uint8_t)((int)bus * 2 + 1), r = (uint8_t)((int)bus * 2 + 2);
    return ctlSet(E_HP, 0x02, l, &x, 2) && ctlSet(E_HP, 0x02, r, &x, 2);
}
bool Device::getBusVolume(Bus bus, float& out) {
    int16_t x = 0; uint8_t l = (uint8_t)((int)bus * 2 + 1);
    if (!ctlGet(E_HP, 0x02, l, &x, 2)) return false;
    out = volToFloat(x); return true;
}

// ---- input ----
bool Device::setInputPhase(int ch, bool on) { uint8_t b = on ? 1 : 0; return ctlSet(E_IN, 0x0d, (uint8_t)ch, &b, 1); }
bool Device::setInputGainRaw(int ch, int16_t v) { return ctlSet(E_IN, 0x0b, (uint8_t)ch, &v, 2); }

// ---- mixer matrix ----
bool Device::setMixSend(int input, Bus bus, float v) {
    int base = input * 6 + (int)bus * 2;   // L cell; R = base+1
    int16_t x = floatToVol(v);
    return ctlSet(E_MIX, 0x01, (uint8_t)base, &x, 2)
        && ctlSet(E_MIX, 0x01, (uint8_t)(base + 1), &x, 2);
}
bool Device::setMixerCell(int cell, float g) { int16_t x = floatToVol(g); return ctlSet(E_MIX, 0x01, (uint8_t)cell, &x, 2); }
bool Device::setMixerCellRaw(int cell, int16_t v) { return ctlSet(E_MIX, 0x01, (uint8_t)cell, &v, 2); }

// ---- routing ----
bool Device::setRouting(int out, uint8_t code) { return ctlSet(E_ROUTE, 0x06, (uint8_t)out, &code, 1); }

// Output-routing source code for a destination, per the device's formula
// (docs/DEVICES.md). Returns -1 if the destination/scheme isn't supported.
// iD24 is hardware-verified; iD48 is RE'd-but-untested (different constants).
static int routingCodeFor(const DeviceInfo* info, Device::OutDest d, int out) {
    if (!info || info->scheme != RoutingScheme::Formula) return -1;
    bool id48 = info->productId == 0x0012;
    int lr = out & 1;                       // L/R within the output's stereo pair
    switch (d) {
        case Device::OutDest::DAW:
        case Device::OutDest::Direct: return id48 ? (out >= 16 ? out - 8 : out) : out;
        case Device::OutDest::Main:   return id48 ? 0x3f + lr : 0x25 + lr;        // type 1
        case Device::OutDest::Alt:    return (id48 ? 0x32 : 0x1e) + 0 + lr;       // type 3 idx 0
        case Device::OutDest::CueA:   return (id48 ? 0x32 : 0x1e) + 2 + lr;       // type 3 idx 1
        case Device::OutDest::CueB:   return (id48 ? 0x32 : 0x1e) + 4 + lr;       // type 3 idx 2
    }
    return -1;
}

bool Device::setOutputRouting(int l, int r, OutDest d) {
    if (!info_ || !info_->hasRouting) { err_ = "device has no routing"; return false; }
    if (info_->scheme == RoutingScheme::Table) {
        err_ = "routing not yet implemented for this model (table scheme — see docs/DEVICES.md)";
        return false;
    }
    int cl = routingCodeFor(info_, d, l), cr = routingCodeFor(info_, d, r);
    if (cl < 0 || cr < 0) { err_ = "unsupported routing destination"; return false; }
    return setRouting(l, (uint8_t)cl) && setRouting(r, (uint8_t)cr);
}

// ---- meters ----
int Device::readMeterBlock(uint16_t off, uint8_t* buf, uint8_t len) {
    return memGet(E_MIX, off, buf, len) ? len : -1;
}

} // namespace monix
