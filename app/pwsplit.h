#pragma once
// Windows-style device split, managed live from the GUI. Each virtual device is a
// pw-loopback child process exposing a clean stereo sink/source wired to one
// channel pair of the iD's multichannel node. Enable = spawn; disable = pkill the
// process by its unique node.name; "running" = the node is present in PipeWire.
// Spawned detached (setsid) so the devices survive closing the app; a persistent
// startup config can also be written.
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

namespace pwsplit {

struct Dev {
    const char* label;     // shown in UI + node.description
    const char* node;      // unique node.name (also the pkill token)
    bool        sink;      // true = output sink, false = input source
    int         auxL, auxR;
};

// The standard iD24 split (matches setup/id24-split.conf).
inline const std::vector<Dev>& devices() {
    static const std::vector<Dev> d = {
        {"Main 1+2",   "id24_main12",   true,  0, 1},
        {"Out 3+4",    "id24_out34",    true,  2, 3},
        {"Phones 5+6", "id24_phones56", true,  4, 5},
        {"Mic 1+2",    "id24_mic12",    false, 0, 1},
        {"ADAT 1+2",   "id24_adat12",   false, 2, 3},
        {"ADAT 3+4",   "id24_adat34",   false, 4, 5},
        {"Loopback",   "id24_loopback", false, 10, 11},
    };
    return d;
}

inline bool pwAvailable() { return system("command -v pw-loopback >/dev/null 2>&1") == 0; }

// First `pactl list short <kind>` line whose name contains both needles -> name.
inline std::string findNode(const char* kind, const char* n1, const char* n2) {
    std::string cmd = std::string("pactl list short ") + kind + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r"); if (!f) return "";
    char line[1024]; std::string res;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, n1) && strstr(line, n2)) {
            const char* a = strchr(line, '\t'); if (!a) continue; a++;
            const char* b = strchr(a, '\t'); if (!b) continue;
            res.assign(a, b - a); break;
        }
    }
    pclose(f); return res;
}
inline std::string outputNode() { return findNode("sinks",   "Audient", "multichannel-output"); }
inline std::string inputNode()  { return findNode("sources", "Audient", "multichannel-input"); }
inline std::string cardNode()   { return findNode("cards",   "Audient", "alsa_card"); }

// Is a virtual node (our node.name) currently present? `pactl list short` takes a
// single type, so check sinks and sources separately.
inline bool running(const char* node) {
    std::string tok = std::string("\t") + node + "\t";
    for (const char* kind : { "sinks", "sources" }) {
        std::string cmd = std::string("pactl list short ") + kind + " 2>/dev/null";
        FILE* f = popen(cmd.c_str(), "r"); if (!f) continue;
        char line[1024]; bool found = false;
        while (fgets(line, sizeof line, f)) if (strstr(line, tok.c_str())) { found = true; break; }
        pclose(f);
        if (found) return true;
    }
    return false;
}

inline std::string posStr(int l, int r) { char b[24]; snprintf(b, sizeof b, "[AUX%d AUX%d]", l, r); return b; }

// Per-device PID file under ~/.cache/monix so we can stop the exact process
// later (instead of pkill -f, which can match unrelated command lines).
inline std::string pidPath(const char* node) {
    const char* home = getenv("HOME"); std::string base = home ? home : "/tmp";
    std::string dir = base + "/.cache/monix";
    std::string mk = "mkdir -p '" + dir + "' 2>/dev/null"; if (system(mk.c_str())) {}
    return dir + "/" + node + ".pid";
}
inline bool procIsLoopbackFor(pid_t pid, const char* node) {
    char p[64]; snprintf(p, sizeof p, "/proc/%d/cmdline", (int)pid);
    FILE* f = fopen(p, "rb"); if (!f) return false;
    char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f);
    for (size_t i = 0; i + 1 < n; i++) if (buf[i] == '\0') buf[i] = ' ';   // NUL-separated args
    buf[n] = '\0';
    return strstr(buf, "pw-loopback") && strstr(buf, node);
}

// Spawn the pw-loopback for one device. Returns true if the child was launched.
inline bool enable(const Dev& d) {
    std::string tgt = d.sink ? outputNode() : inputNode();
    if (tgt.empty()) return false;
    std::string cap, pb;
    if (d.sink) {
        cap = std::string("media.class=Audio/Sink node.name=") + d.node +
              " node.description=\"iD24 " + d.label + "\" audio.position=[FL FR]";
        pb  = std::string("node.name=") + d.node + ".pb audio.position=" + posStr(d.auxL, d.auxR) +
              " node.target=" + tgt + " stream.dont-remix=true node.passive=true";
    } else {
        cap = std::string("node.name=") + d.node + ".cap audio.position=" + posStr(d.auxL, d.auxR) +
              " node.target=" + tgt + " stream.dont-remix=true node.passive=true";
        pb  = std::string("media.class=Audio/Source node.name=") + d.node +
              " node.description=\"iD24 " + d.label + "\" audio.position=[FL FR]";
    }
    // Double-fork: the grandchild (pw-loopback) is reparented to init and reaped
    // there, so the GUI accrues no zombies and needs no SIGCHLD handler (which
    // would otherwise break system()/popen() return codes elsewhere here).
    std::string pidf = pidPath(d.node);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        pid_t gc = fork();
        if (gc == 0) {
            int n = open("/dev/null", O_RDWR); if (n >= 0) { dup2(n, 1); dup2(n, 2); }
            execlp("pw-loopback", "pw-loopback",
                   "--capture-props", cap.c_str(), "--playback-props", pb.c_str(), (char*)nullptr);
            _exit(127);
        }
        if (gc > 0) { std::ofstream(pidf) << gc; }   // record grandchild pid
        _exit(0);                                     // grandchild adopted by init (no zombie)
    }
    int st; waitpid(pid, &st, 0); // reap the intermediate child immediately
    return true;
}

// Stop a device: kill the recorded PID after confirming it's really our
// pw-loopback (guards against PID reuse). No pkill -f, so nothing else matches.
inline void disable(const Dev& d) {
    std::string pidf = pidPath(d.node);
    std::ifstream in(pidf); pid_t pid = 0; in >> pid;
    if (pid > 0 && procIsLoopbackFor(pid, d.node)) kill(pid, SIGTERM);
    remove(pidf.c_str());
}

// Switch the card to the duplex profile (enables inputs). Returns true on success.
inline bool setDuplex() {
    std::string card = cardNode(); if (card.empty()) return false;
    std::string cmd = "pactl set-card-profile " + card +
                      " output:multichannel-output+input:multichannel-input >/dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

// Write a persistent PipeWire startup config for the given enabled devices.
inline bool saveConfig(const bool* enabled, std::string& pathOut) {
    const char* home = getenv("HOME"); if (!home) return false;
    std::string dir = std::string(home) + "/.config/pipewire/pipewire.conf.d";
    std::string mk = "mkdir -p '" + dir + "'"; if (system(mk.c_str())) {}
    std::string path = dir + "/id24-split.conf";
    std::ofstream f(path); if (!f) return false;
    std::string out = outputNode(), in = inputNode();
    f << "# Generated by Monix. Stereo split of the Audient iD multichannel device.\n";
    f << "context.modules = [\n";
    auto& dv = devices();
    for (size_t i = 0; i < dv.size(); i++) {
        if (!enabled[i]) continue;
        const Dev& d = dv[i];
        std::string tgt = d.sink ? out : in;
        f << "  { name = libpipewire-module-loopback args = { node.description=\"iD24 " << d.label << "\"\n";
        if (d.sink) {
            f << "      capture.props  = { media.class=Audio/Sink node.name=" << d.node << " audio.position=[FL FR] }\n";
            f << "      playback.props = { node.name=" << d.node << ".pb audio.position=" << posStr(d.auxL,d.auxR)
              << " node.target=" << tgt << " stream.dont-remix=true node.passive=true } } }\n";
        } else {
            f << "      capture.props  = { node.name=" << d.node << ".cap audio.position=" << posStr(d.auxL,d.auxR)
              << " node.target=" << tgt << " stream.dont-remix=true node.passive=true }\n";
            f << "      playback.props = { media.class=Audio/Source node.name=" << d.node << " audio.position=[FL FR] } } }\n";
        }
    }
    f << "]\n";
    pathOut = path;
    return true;
}

} // namespace pwsplit
