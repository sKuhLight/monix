#pragma once
#include "appsettings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>

namespace drivercheck {

struct Status {
    bool helper = false;
    bool usb = false;
    bool module = false;
    bool node = false;
    bool olderModule = false;
    bool dkms = false;
    bool headers = false;
    bool needsRebuild = false;
    std::string kernel;
    std::string reason;
    std::string helperPath;
};

inline std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

inline bool executable(const std::string& p) {
    return !p.empty() && access(p.c_str(), X_OK) == 0;
}

inline std::string dirName(std::string p) {
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return p.substr(0, slash);
}

inline std::string helperPath() {
    std::vector<std::string> candidates;
    std::string exeDir = dirName(currentExecutablePath());
    candidates.push_back(exeDir + "/monix-driverctl");
    candidates.push_back(exeDir + "/../monix-driverctl");
#ifdef MONIX_SOURCE_DIR
    candidates.push_back(std::string(MONIX_SOURCE_DIR) + "/tools/monix-driverctl");
#endif
    candidates.push_back("/usr/local/bin/monix-driverctl");
    candidates.push_back("/usr/bin/monix-driverctl");
    for (const auto& p : candidates) if (executable(p)) return p;
    return "";
}

inline Status readStatus() {
    Status s;
    s.helperPath = helperPath();
    if (s.helperPath.empty()) return s;
    s.helper = true;

    std::string cmd = shellQuote(s.helperPath) + " status 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return s;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string x(line);
        while (!x.empty() && (x.back() == '\n' || x.back() == '\r')) x.pop_back();
        size_t eq = x.find('=');
        if (eq == std::string::npos) continue;
        std::string k = x.substr(0, eq);
        std::string v = x.substr(eq + 1);
        if (k == "kernel") s.kernel = v;
        else if (k == "usb") s.usb = v == "present";
        else if (k == "module") s.module = v == "current";
        else if (k == "node") s.node = v == "present";
        else if (k == "older_module") s.olderModule = v == "yes";
        else if (k == "dkms") s.dkms = v == "yes";
        else if (k == "headers") s.headers = v == "yes";
        else if (k == "needs_rebuild") s.needsRebuild = v == "yes";
        else if (k == "reason") s.reason = v;
    }
    pclose(f);
    return s;
}

inline bool startRepair(const Status& s) {
    if (!s.helper) return false;
    std::string log = "/tmp/monix-driverctl.log";
    std::string cmd;
#ifdef MONIX_SOURCE_DIR
    cmd += "MONIX_SOURCE_DIR=" + shellQuote(MONIX_SOURCE_DIR) + " ";
#endif
    cmd += shellQuote(s.helperPath) + " install-current >" + shellQuote(log) + " 2>&1 &";
    return system(cmd.c_str()) == 0;
}

inline std::string repairLogTail() {
    std::ifstream f("/tmp/monix-driverctl.log");
    if (!f) return "";
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
        if (lines.size() > 8) lines.erase(lines.begin());
    }
    std::string out;
    for (const auto& l : lines) {
        if (!out.empty()) out += "\n";
        out += l;
    }
    return out;
}

} // namespace drivercheck
