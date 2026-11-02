#pragma once
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

struct AppSettings {
    bool trayEnabled = true;
    bool closeToTray = true;
};

inline std::string monixConfigDir() {
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    mkdir((base + "/.config").c_str(), 0755);
    std::string dir = base + "/.config/monix";
    mkdir(dir.c_str(), 0755);
    return dir;
}

inline std::string appSettingsPath() {
    return monixConfigDir() + "/app.conf";
}

inline bool loadAppSettings(AppSettings& s) {
    std::ifstream f(appSettingsPath());
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string k;
        int v = 0;
        if (ss >> k >> v) {
            if (k == "tray") s.trayEnabled = v != 0;
            else if (k == "close_to_tray") s.closeToTray = v != 0;
        }
    }
    return true;
}

inline void saveAppSettings(const AppSettings& s) {
    std::ofstream f(appSettingsPath());
    if (!f) return;
    f << "tray " << (s.trayEnabled ? 1 : 0) << '\n';
    f << "close_to_tray " << (s.closeToTray ? 1 : 0) << '\n';
}

inline std::string currentExecutablePath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "monix-gui";
    buf[n] = '\0';
    return buf;
}

inline std::string desktopQuote(const std::string& s) {
    bool needsQuote = s.find_first_of(" \t\n\"'\\") != std::string::npos;
    if (!needsQuote) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            out += '\\';
            out += c;
        }
        else out += c;
    }
    out += "\"";
    return out;
}

inline std::string autostartPath() {
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    mkdir((base + "/.config").c_str(), 0755);
    std::string dir = base + "/.config/autostart";
    mkdir(dir.c_str(), 0755);
    return dir + "/monix.desktop";
}

inline bool autostartEnabled() {
    std::ifstream f(autostartPath());
    return f.good();
}

inline bool setAutostart(bool enabled) {
    std::string path = autostartPath();
    if (!enabled) return remove(path.c_str()) == 0 || errno == ENOENT;

    std::ofstream f(path);
    if (!f) return false;
    f << "[Desktop Entry]\n";
    f << "Type=Application\n";
    f << "Name=Monix\n";
    f << "Comment=Audient iD control panel\n";
    f << "Exec=" << desktopQuote(currentExecutablePath()) << " --start-hidden\n";
    f << "Icon=audio-card\n";
    f << "Terminal=false\n";
    f << "Categories=Audio;AudioVideo;Mixer;\n";
    f << "X-GNOME-Autostart-enabled=true\n";
    return true;
}

inline std::string launcherPath() {
    const char* home = getenv("HOME");
    std::string base = home ? home : ".";
    mkdir((base + "/.local").c_str(), 0755);
    mkdir((base + "/.local/share").c_str(), 0755);
    std::string dir = base + "/.local/share/applications";
    mkdir(dir.c_str(), 0755);
    return dir + "/monix.desktop";
}

inline bool installLauncher() {
    std::ofstream f(launcherPath());
    if (!f) return false;
    f << "[Desktop Entry]\n";
    f << "Type=Application\n";
    f << "Name=Monix\n";
    f << "Comment=Audient iD control panel\n";
    f << "Exec=" << desktopQuote(currentExecutablePath()) << "\n";
    f << "Icon=audio-card\n";
    f << "Terminal=false\n";
    f << "StartupWMClass=monix\n";
    f << "Categories=Audio;AudioVideo;Mixer;\n";
    return true;
}
