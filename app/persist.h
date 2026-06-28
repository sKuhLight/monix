#pragma once
// Persistence for the write-only controls (faders, phase, phones). The iD24
// firmware does not report these back over USB, so Monix remembers what it last
// set and restores them on launch. (Monitor volume + master toggles are readable,
// so those come from the device instead.)
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>

struct MonixState {
    std::vector<float> faders;
    std::vector<int>   phase;
    std::vector<int>   links;     // stereo-link mask per node pair (8)
    float              phones = -1.0f;

    bool operator==(const MonixState& o) const {
        return faders == o.faders && phase == o.phase && links == o.links && phones == o.phones;
    }
    bool operator!=(const MonixState& o) const { return !(*this == o); }
};

inline std::string monixCfgPath() {
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    mkdir((base + "/.config").c_str(), 0755);
    std::string dir = base + "/.config/monix";
    mkdir(dir.c_str(), 0755);
    return dir + "/state.conf";
}

inline void saveState(const MonixState& s) {
    std::ofstream f(monixCfgPath());
    if (!f) return;
    f << "faders"; for (float x : s.faders) f << ' ' << x; f << '\n';
    f << "phase";  for (int x : s.phase)    f << ' ' << x; f << '\n';
    f << "links";  for (int x : s.links)    f << ' ' << x; f << '\n';
    f << "phones " << s.phones << '\n';
}

inline bool loadState(MonixState& s) {
    std::ifstream f(monixCfgPath());
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line); std::string k; ss >> k;
        if (k == "faders") { s.faders.clear(); float x; while (ss >> x) s.faders.push_back(x); }
        else if (k == "phase") { s.phase.clear(); int x; while (ss >> x) s.phase.push_back(x); }
        else if (k == "links") { s.links.clear(); int x; while (ss >> x) s.links.push_back(x); }
        else if (k == "phones") ss >> s.phones;
    }
    return true;
}
