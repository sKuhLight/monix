// Monix protocol library — Audient iD control via the audient_id kernel module.
// Control map reverse-engineered from a live USB capture of the official app;
// see docs/PROTOCOL.md (★ CAPTURED CONTROL MAP).
//
// All controls: bRequest=0x01, SET=bmRequestType 0x21 / GET=0xA1,
// wValue=(selector<<8)|channel, wIndex=(entity<<8). Meters: req=0x03 (GET_MEM).
#pragma once
#include <cstdint>

namespace monix {

constexpr uint16_t kVendorId = 0x2708;

struct DeviceInfo { const char* name; uint16_t productId; int micInputs, digitalInputs, outputs, digitalOutputs; };
const DeviceInfo* findDevice(uint16_t productId);

enum class Master { Mono, MonoMode, Phase, Cut, Dim, Talkback, Alt };   // entity 0x36 toggles
enum class MonoMode { Center = 0, Left = 1, Right = 2 };
enum class TalkbackSource { Mic1 = 0x10, Mic2 = 0x11, Digi1 = 0x12 };

class Device {
public:
    Device() = default;
    ~Device();
    bool open(const char* path = nullptr);
    bool isOpen() const { return fd_ >= 0; }
    void close();
    const char* lastError() const { return err_; }

    // ---- Monitor section (entity 0x36) ----
    bool setMaster(Master m, bool on);          // Mono/Phase/Cut/Dim/Talkback/Alt
    bool getMaster(Master m, bool& out);
    bool setMonoMode(MonoMode m);               // sel 0x01: Center/Left/Right
    bool setMonitorVolume(float v);             // sel 0x12
    bool getMonitorVolume(float& out);
    bool setDimTrim(float v);                    // sel 0x06
    bool setAltTrim(float v);                    // sel 0x17
    bool setTalkbackSource(TalkbackSource s);   // sel 0x08

    // Clock source via ALSA enum ("Audient Clock Selector Clock Source"):
    // 0 = Internal, 1 = Optical (S/PDIF or ADAT input). When feeding the optical
    // input, slave to it (index 1) or you get clock-drift crackle.
    bool setClockSource(int index);
    int  getClockSource();           // -1 on fail
    bool getOpticalClockValid();     // true if a valid clock is present on optical in

    // ---- Audible monitor volume via ALSA (snd-usb-audio "Speaker Playback Volume") ----
    // On Linux this is the DAW->output level the OS controls; the 0x36 monitor
    // controller only governs the hardware input-monitor mix. Use this for the
    // main monitor knob so it actually changes what you hear (and stays balanced).
    bool setSpeakerVolume(float v);
    bool getSpeakerVolume(float& out);

    // ---- Sample rate / clock ----
    bool setSampleRate(int hz);                 // entity 0x29 sel 0x01 (int32)
    int  getSampleRate();                       // entity 0x3e sel 0x06 (int32), -1 on fail

    // ---- Digital I/O optical mode (false = S/PDIF, true = ADAT) ----
    bool setDigitalInputADAT(bool adat);        // entity 0x01 sel 0x00 (int32 0/1)
    bool setDigitalOutputADAT(bool adat);       // entity 0x14 sel 0x01 (int32 0/1)

    // ---- Headphones (entity 0x0c sel 0x02) ----
    bool setHeadphoneVolume(int hp /*0 or 1*/, float v);

    // ---- Input channels (entity 0x0b) ----
    bool setInputPhase(int channel /*1-based*/, bool on);   // sel 0x0d
    bool setInputGainRaw(int channel /*1-based*/, int16_t v);// sel 0x0b

    // ---- Mixer matrix (entity 0x3c sel 0x01, 96 cells = 16 inputs x 6 sends) ----
    // cell = input*6 + send ; send 0/1 = Main L/R, 2/3 = Cue A L/R, 4/5 = Cue B L/R.
    enum class Bus { Main = 0, CueA = 1, CueB = 2 };
    // Set an input's stereo send to a bus (writes the L and R cell together).
    bool setMixSend(int input /*0..15*/, Bus bus, float v);
    bool setMixerCell(int cell /*0..95*/, float gain);
    bool setMixerCellRaw(int cell, int16_t v);

    // ---- Routing (entity 0x33 sel 0x06): output <- source code ----
    // What an output pair plays (source codes captured from the official app).
    enum class OutDest { Main, Alt, CueA, CueB, DAW, Direct };
    bool setRouting(int output, uint8_t sourceCode);
    // Route a stereo output pair to a destination (writes both L and R codes).
    bool setOutputRouting(int leftOut, int rightOut, OutDest d);

    // ---- Meters: GET_MEM block on entity 0x3c ----
    // offset/len per docs (0:32, 1:12, 2:16, 3:6). Returns bytes read or -1.
    int  readMeterBlock(uint16_t offset, uint8_t* buf, uint8_t len);

    // ---- Low-level ----
    bool ctlSet(uint8_t entity, uint8_t sel, uint8_t ch, const void* d, uint8_t len);
    bool ctlGet(uint8_t entity, uint8_t sel, uint8_t ch, void* d, uint8_t len);
    bool memGet(uint8_t entity, uint16_t offset, void* d, uint8_t len);

private:
    bool xfer(uint8_t bmReqType, uint8_t bReq, uint16_t wValue, uint16_t wIndex,
              void* data, uint8_t len, bool in);
    int  fd_ = -1;
    const char* err_ = "";
};

// Volume scaling: device uses int16 in 1/256 dB (0x0000 = 0 dB, 0x8000 = mute).
int16_t floatToVol(float v);   // 0..1 -> int16 (dB curve)
float   volToFloat(int16_t v); // inverse

} // namespace monix
