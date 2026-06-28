// Monix — native Linux control panel for Audient iD interfaces.
// Layout modeled on the official iD mixer: input/DAW channel strips | Master Mix
// (+Cue A/B) | monitor panel. Dark-violet theme. Talks to the device through the
// audient_id kernel module so audio keeps playing.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "monix.h"
#include "theme.h"
#include "persist.h"
#include "pwsplit.h"

#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <fstream>

using namespace monix;
using clk = std::chrono::steady_clock;

static double now_ms() {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
               clk::now().time_since_epoch()).count();
}
static uint16_t detectPid() {
    DIR* d = opendir("/sys/bus/usb/devices"); if (!d) return 0x000d;
    uint16_t pid = 0;
    for (dirent* e; (e = readdir(d));) {
        std::string b = std::string("/sys/bus/usb/devices/") + e->d_name;
        std::ifstream vf(b + "/idVendor"); std::string v;
        if (!(vf >> v) || v != "2708") continue;
        std::ifstream pf(b + "/idProduct"); std::string p;
        if (pf >> p) { pid = (uint16_t)strtol(p.c_str(), nullptr, 16); break; }
    }
    closedir(d); return pid ? pid : 0x000d;
}

// Mixer nodes are, in device order (from RE: defaultMixMap): the analogue mics,
// then the digital/optical inputs, then the DAW returns. Names are generated from
// the device profile (counts). A visible strip is one mono node or a linked stereo
// pair (node, node+1).
struct VStrip { std::string name; int node; bool stereo; bool mic; };

int main() {
    if (!glfwInit()) { fprintf(stderr, "glfw init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* win = glfwCreateWindow(1320, 800, "Monix", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win); glfwSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    monixtheme::apply();

    Device dev; bool connected = dev.open();
    const DeviceInfo* info = dev.info();             // set from the connected PID
    if (!info) info = findDevice(detectPid());        // not connected: best-effort by sysfs
    if (!info) info = findDevice(0x000d);             // last resort: iD24 layout
    const char* devName = info->name;

    // Mixer geometry from the profile.
    const int NODES     = info->hasMixer ? info->mixerNodes() : 0;
    const int firstDigi = info->micInputs;                       // mics: [0, firstDigi)
    const int firstDaw  = info->micInputs + info->digitalInputs; // DAW: [firstDaw, NODES)
    auto nodeName = [&](int n) -> std::string {
        char b[24];
        if (n < firstDigi)     snprintf(b, sizeof b, "Mic %d", n + 1);
        else if (n < firstDaw) snprintf(b, sizeof b, "Digi %d", n - firstDigi + 1);
        else                   snprintf(b, sizeof b, "DAW %d", n - firstDaw + 1);
        return b;
    };
    auto pairName = [&](int a) -> std::string {
        char b[32];
        if (a < firstDigi)     snprintf(b, sizeof b, "Mic %d+%d", a + 1, a + 2);
        else if (a < firstDaw) snprintf(b, sizeof b, "Digi %d+%d", a-firstDigi+1, a-firstDigi+2);
        else                   snprintf(b, sizeof b, "DAW %d+%d", a-firstDaw+1, a-firstDaw+2);
        return b;
    };

    // Stereo-link mask, one entry per adjacent node pair. DAW returns default linked.
    const int nPairs = NODES / 2;
    std::vector<char> link(nPairs, 0);
    for (int p = 0; p < nPairs; p++) if (2 * p >= firstDaw) link[p] = 1;
    auto buildStrips = [&]() {
        std::vector<VStrip> v;
        for (int p = 0; p < nPairs; p++) {
            int a = p * 2;
            if (link[p]) v.push_back({pairName(a), a, true, a < firstDigi});
            else { v.push_back({nodeName(a), a, false, a < firstDigi});
                   v.push_back({nodeName(a+1), a+1, false, (a+1) < firstDigi}); }
        }
        if (NODES & 1) v.push_back({nodeName(NODES-1), NODES-1, false, (NODES-1) < firstDigi});
        return v;
    };
    std::vector<VStrip> strips = buildStrips();

    // UI state indexed by NODE so it survives link/unlink changes (min size 1).
    const int NST = NODES > 0 ? NODES : 1;
    std::vector<float> fader(NST, 0.75f), pan(NST, 0.0f), meter(NST, 0.0f);
    std::vector<bool>  mute(NST, false), solo(NST, false), phase(NST, false);
    float mainVol = 0.6f;
    bool master[6] = {false,false,false,false,false,false};       // Dim,Alt,Talk,Phase,Mono,Cut
    const char* masterName[6] = {"DIM","ALT","TB","\xC3\x98","MONO","CUT"};
    const Master masterEnum[6] = {Master::Dim, Master::Alt, Master::Talkback, Master::Phase, Master::Mono, Master::Cut};
    int   curMix = 0;            // 0=Main,1=Cue A,2=Cue B (which mix the faders edit)
    const char* mixName[3] = {"MASTER MIX", "CUE A", "CUE B"};
    int   source = 2;           // MIC/OPT/DAW selector (display)
    bool  showRouting = false, showDevices = false;
    struct OutPair { std::string name; int l, r; };
    // Routable analogue output pairs, generated from the profile's output count.
    std::vector<OutPair> outPairs;
    for (int o = 0; o + 1 < info->outputs; o += 2) {
        char nm[24];
        if (o == 0)                          snprintf(nm, sizeof nm, "Main 1+2");
        else if (o + 2 >= info->outputs)     snprintf(nm, sizeof nm, "Phones %d+%d", o+1, o+2);
        else                                 snprintf(nm, sizeof nm, "Out %d+%d", o+1, o+2);
        outPairs.push_back({nm, o, o + 1});
    }
    std::vector<int> routeSel(outPairs.size(), 0); // index into OutDest (Main..DAW)
    float outMeter[6] = {0,0,0,0,0,0};   // Master L/R, Cue A L/R, Cue B L/R (off1)
    bool  mainActive = false; int sampleRate = 0; bool meterEnabled = true;
    double lastState = 0, lastMeter = 0;

    // restore persisted faders/pan/phase
    MonixState saved;
    if (loadState(saved)) {
        for (int i = 0; i < NODES && i < (int)saved.faders.size(); i++) fader[i] = saved.faders[i];
        for (int i = 0; i < NODES && i < (int)saved.phase.size(); i++)  phase[i] = saved.phase[i] != 0;
        for (int i = 0; i < nPairs && i < (int)saved.links.size(); i++) link[i] = saved.links[i] != 0;
        strips = buildStrips();
    }
    auto snap = [&]{ MonixState s; s.faders = fader; s.phones = -1;
        for (bool b : phase) s.phase.push_back(b?1:0);
        for (bool b : link)  s.links.push_back(b?1:0); return s; };

    // push a strip's level+pan to the current mix's L/R cells. State is per-node;
    // a stereo strip drives node (L) + node+1 (R) from the same fader/pan.
    auto pushStrip = [&](const VStrip& s) {
        if (!connected) return;
        int n = s.node;
        float v = mute[n] ? 0.0f : fader[n];
        float p = pan[n];
        float gl = v * (p > 0 ? 1.0f - p : 1.0f);
        float gr = v * (p < 0 ? 1.0f + p : 1.0f);
        if (s.stereo) {                          // L src->busL, R src->busR; cross-feed muted
            dev.setMixerCell(n * 6 + curMix * 2 + 0, gl);        // L src -> bus L
            dev.setMixerCell(n * 6 + curMix * 2 + 1, 0.0f);      // L src -> bus R (off)
            dev.setMixerCell((n + 1) * 6 + curMix * 2 + 0, 0.0f);// R src -> bus L (off)
            dev.setMixerCell((n + 1) * 6 + curMix * 2 + 1, gr);  // R src -> bus R
        } else {                                 // mono source panned across bus
            dev.setMixerCell(n * 6 + curMix * 2 + 0, gl);
            dev.setMixerCell(n * 6 + curMix * 2 + 1, gr);
        }
    };
    // Re-push every strip across all three mix buses (used after a link change so
    // stale cross-feed cells from the previous layout get cleared on the device).
    auto pushAll = [&]{
        int savedMix = curMix;
        for (curMix = 0; curMix < 3; curMix++) for (auto& s : strips) pushStrip(s);
        curMix = savedMix;
    };

    auto knob = [&](const char* id, float* v, float sz) -> bool {
        // simple round knob via an invisible drag + custom draw
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos(); float r = sz * 0.5f;
        ImGui::InvisibleButton("k", ImVec2(sz, sz));
        bool changed = false;
        if (ImGui::IsItemActive()) { float d = -ImGui::GetIO().MouseDelta.y * 0.005f; *v += d; if (*v<0)*v=0; if(*v>1)*v=1; changed = true; }
        ImVec2 c = ImVec2(p.x + r, p.y + r);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(c, r, ImGui::ColorConvertFloat4ToU32(monixtheme::kPanel2), 32);
        float a0 = 2.356f, a1 = 0.785f; float a = a0 + (a1 - a0 + 6.283f) * 0; // base
        float ang = 2.356f + (*v) * (6.283f - 1.571f);
        dl->PathArcTo(c, r, 2.356f, 2.356f + (*v)*(7.069f-2.356f), 24);
        dl->PathStroke(ImGui::ColorConvertFloat4ToU32(monixtheme::kAccent), 0, 3.0f);
        dl->AddLine(c, ImVec2(c.x + cosf(ang)*r*0.8f, c.y + sinf(ang)*r*0.8f),
                    ImGui::ColorConvertFloat4ToU32(monixtheme::kAccentHi), 2.5f);
        (void)a;(void)a0;(void)a1;
        ImGui::PopID();
        return changed;
    };
    auto vmeter = [&](float lv, ImVec2 sz) {
        ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x+sz.x,p.y+sz.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f,0.06f,0.09f,1)), 2);
        if (lv > 0.001f) { float h = sz.y*lv; dl->AddRectFilled(ImVec2(p.x,p.y+sz.y-h), ImVec2(p.x+sz.x,p.y+sz.y), monixtheme::meterColor(lv), 2); }
        ImGui::Dummy(sz);
    };
    auto toggle = [&](const char* lbl, bool on, ImVec2 sz) -> bool {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, monixtheme::kAccent);
        bool c = ImGui::Button(lbl, sz);
        if (on) ImGui::PopStyleColor();
        return c;
    };

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (connected) {
            double t = now_ms();
            if (t - lastState > 500) { lastState = t;
                sampleRate = dev.getSampleRate();
                float mv; if (!mainActive && dev.getMonitorVolume(mv)) mainVol = mv;
                for (int m=0;m<6;m++){ bool b=false; if (dev.getMaster(masterEnum[m],b)) master[m]=b; }
            }
            if (meterEnabled && t - lastMeter > 70) { lastMeter = t;
                uint8_t blk[32]={0};
                if (dev.readMeterBlock(0, blk, 32) > 0)   // up to 16 input meters, uint16 LE
                    for (int n=0;n<NODES && n<16;n++){    // contiguous in node order (RE)
                        float lv = (blk[n*2] | (blk[n*2+1]<<8)) / 65535.0f;
                        meter[n]=meter[n]*0.5f+lv*0.5f; }
                uint8_t ob[12]={0};
                if (dev.readMeterBlock(1, ob, 12) > 0)    // Master L/R, Cue A L/R, Cue B L/R
                    for (int k=0;k<6;k++){ float lv=(ob[k*2]|(ob[k*2+1]<<8))/65535.0f; outMeter[k]=outMeter[k]*0.5f+lv*0.5f; }
            }
        }

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##monix", nullptr, ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBringToFrontOnFocus);

        float H = io.DisplaySize.y, W = io.DisplaySize.x;
        float rightW = 250, midW = 210;
        float stripsW = W - rightW - midW - 28;

        // ===== channel strips =====
        ImGui::BeginChild("strips", ImVec2(stripsW, H-20), true, ImGuiWindowFlags_HorizontalScrollbar);
        bool relink = false;
        for (int i=0;i<(int)strips.size();i++) {
            ImGui::PushID(i); ImGui::BeginGroup();
            const VStrip& s = strips[i];
            int n = s.node;
            ImGui::TextUnformatted(s.name.c_str());
            // gain badge + phase (mics)
            if (s.mic) { ImGui::SmallButton("10dB"); ImGui::SameLine();
                if (toggle("\xC3\x98", phase[n], ImVec2(24,0))) { phase[n]=!phase[n]; if(connected) dev.setInputPhase(n+1, phase[n]); } }
            else ImGui::Dummy(ImVec2(0,20));
            // pan / balance slider
            float pv = (pan[n]+1)*0.5f;
            ImGui::SetNextItemWidth(54);
            if (ImGui::SliderFloat("##pan", &pv, 0,1, s.stereo?"BAL":"PAN")) { pan[n]=pv*2-1; pushStrip(s); }
            // STEREO/MONO link toggle (links node pair n/n+1)
            if (n/2 < nPairs) {
                if (toggle(s.stereo?"STEREO":"MONO", s.stereo, ImVec2(60,0))) { link[n/2] = !link[n/2]; relink = true; }
            } else ImGui::TextDisabled("MONO");
            // solo / mute
            if (toggle("S", solo[n], ImVec2(25,0))) solo[n]=!solo[n]; ImGui::SameLine();
            if (toggle("M", mute[n], ImVec2(25,0))) { mute[n]=!mute[n]; pushStrip(s); }
            // fader + meter(s)
            ImGui::BeginGroup();
            if (ImGui::VSliderFloat("##f", ImVec2(34, H-300), &fader[n], 0.0f, 1.0f, "")) pushStrip(s);
            ImGui::EndGroup(); ImGui::SameLine();
            ImGui::BeginGroup();
            if (s.stereo) { vmeter(meter[n], ImVec2(8, H-300)); ImGui::SameLine(0,2); vmeter(meter[n+1], ImVec2(8, H-300)); }
            else            vmeter(meter[n], ImVec2(8, H-300));
            ImGui::EndGroup();
            ImGui::EndGroup(); ImGui::SameLine(0, 16); ImGui::PopID();
        }
        ImGui::EndChild();
        if (relink) { strips = buildStrips(); pushAll(); }   // apply new layout, clear stale cross-cells

        // ===== master mix / cue =====
        ImGui::SameLine();
        ImGui::BeginChild("master", ImVec2(midW, H-20), true);
        for (int m=0;m<3;m++) {
            bool sel = curMix==m;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, monixtheme::kAccent);
            if (ImGui::Button(mixName[m], ImVec2(120, m==0?54:40))) curMix=m;
            if (sel) ImGui::PopStyleColor();
            // stereo meter for this mix bus (Master=0/1, CueA=2/3, CueB=4/5)
            ImGui::SameLine();
            ImGui::BeginGroup();
            vmeter(outMeter[m*2],   ImVec2(7, m==0?54:40)); ImGui::SameLine(0,2);
            vmeter(outMeter[m*2+1], ImVec2(7, m==0?54:40));
            ImGui::EndGroup();
        }
        ImGui::Dummy(ImVec2(0,8)); ImGui::TextDisabled("editing: %s", mixName[curMix]);
        ImGui::EndChild();

        // ===== monitor panel =====
        ImGui::SameLine();
        ImGui::BeginChild("monitor", ImVec2(0, H-20), true);
        ImGui::PushStyleColor(ImGuiCol_Text, monixtheme::kAccentHi);
        ImGui::SetWindowFontScale(1.4f); ImGui::TextUnformatted("MONIX"); ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s   %d Hz", devName, sampleRate);
        if (!connected) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f,0.4f,0.4f,1)); ImGui::TextUnformatted("no device"); ImGui::PopStyleColor(); }
        else if (!info->verified) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.6f,0.2f,1));
            ImGui::TextWrapped("experimental: %s is reverse-engineered but untested", devName);
            ImGui::PopStyleColor(); }
        ImGui::Separator();
        // source select MIC / OPT / DAW
        const char* src[3] = {"MIC","OPT","DAW"};
        for (int k=0;k<3;k++){ if (k) ImGui::SameLine(); if (toggle(src[k], source==k, ImVec2(60,26))) source=k; }
        ImGui::Dummy(ImVec2(0,16));
        // big monitor knob
        ImGui::SetCursorPosX((ImGui::GetWindowWidth()-90)*0.5f);
        if (knob("main", &mainVol, 90)) { mainActive=true; if(connected) dev.setMonitorVolume(mainVol); }
        mainActive = ImGui::IsItemActive();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth()-40)*0.5f); ImGui::Text("%d%%", (int)(mainVol*100));
        ImGui::Dummy(ImVec2(0,16));
        // TB Ø MONO / ALT DIM CUT  (Dim,Alt,Talk,Phase,Mono,Cut)
        struct { const char* l; int idx; } btn[6] = {{"TB",2},{"\xC3\x98",3},{"MONO",4},{"ALT",1},{"DIM",0},{"CUT",5}};
        for (int k=0;k<6;k++){ if (k%3) ImGui::SameLine();
            if (toggle(btn[k].l, master[btn[k].idx], ImVec2(62,34))) { master[btn[k].idx]=!master[btn[k].idx]; if(connected) dev.setMaster(masterEnum[btn[k].idx], master[btn[k].idx]); } }
        ImGui::Dummy(ImVec2(0,10));
        // headphone output levels (entity 0x0c)
        ImGui::TextDisabled("PHONES");
        static float hp1 = 0.6f, hp2 = 0.6f;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##hp1", &hp1, 0,1, "HP1 %.0f%%", ImGuiSliderFlags_AlwaysClamp) && connected) dev.setHeadphoneVolume(0, hp1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##hp2", &hp2, 0,1, "HP2 %.0f%%", ImGuiSliderFlags_AlwaysClamp) && connected) dev.setHeadphoneVolume(1, hp2);
        ImGui::Dummy(ImVec2(0,6));
        // talkback source
        ImGui::TextDisabled("TALKBACK SRC"); ImGui::SetNextItemWidth(-1);
        static int tbSrc = 0; const char* tbN[3] = {"Mic 1","Mic 2","Digi 1"};
        const TalkbackSource tbE[3] = {TalkbackSource::Mic1, TalkbackSource::Mic2, TalkbackSource::Digi1};
        if (ImGui::Combo("##tbsrc", &tbSrc, tbN, 3) && connected) dev.setTalkbackSource(tbE[tbSrc]);
        ImGui::Dummy(ImVec2(0,10));
        if (ImGui::Button("SYSTEM / ROUTING", ImVec2(-1, 30))) showRouting = !showRouting;
        if (ImGui::Button("VIRTUAL DEVICES", ImVec2(-1, 26))) showDevices = !showDevices;
        ImGui::EndChild();

        // ===== System panel: routing matrix (output pair x destination) =====
        if (showRouting) {
            ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
            ImGui::Begin("System Panel \xE2\x80\x94 Routing", &showRouting);
            bool routingReady = info->hasRouting && info->scheme == RoutingScheme::Formula;
            if (!routingReady) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.6f,0.2f,1));
                ImGui::TextWrapped(info->hasRouting
                    ? "Routing for this model is reverse-engineered but not yet wired up "
                      "(table scheme \xE2\x80\x94 see docs/DEVICES.md). Mixer/faders work; "
                      "output routing is read-only for now."
                    : "This model has no software output routing.");
                ImGui::PopStyleColor(); ImGui::Separator();
            }
            const char* dest[5] = {"Main Mix","Alt Spk","Cue A","Cue B","DAW Thru"};
            if (routingReady && ImGui::BeginTable("routing", 6, ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Output");
                for (int c=0;c<5;c++) ImGui::TableSetupColumn(dest[c]);
                ImGui::TableHeadersRow();
                static int pendPair = -1, pendPrev = 0;   // DAW-Thru confirm
                for (size_t p=0;p<outPairs.size();p++) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(outPairs[p].name.c_str());
                    for (int c=0;c<5;c++) {
                        ImGui::TableNextColumn(); ImGui::PushID((int)p*8+c);
                        int before = routeSel[p];
                        if (ImGui::RadioButton("##r", &routeSel[p], c)) {
                            if (c == 4) {                 // DAW Thru = direct full-level out: confirm first
                                pendPair = (int)p; pendPrev = before;
                                ImGui::OpenPopup("dawthru");
                            } else if (connected) {
                                dev.setOutputRouting(outPairs[p].l, outPairs[p].r, (Device::OutDest)c);
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
                // DAW Thru confirmation
                if (ImGui::BeginPopupModal("dawthru", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.5f,0.3f,1));
                    ImGui::TextUnformatted("\xE2\x9A\xA0  DAW Thru = DIRECT output, full level,");
                    ImGui::TextUnformatted("    NO volume control. This can damage");
                    ImGui::TextUnformatted("    speakers and your hearing.");
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0,6));
                    if (ImGui::Button("Set DAW Thru", ImVec2(140,0))) {
                        if (connected && pendPair>=0) dev.setOutputRouting(outPairs[pendPair].l, outPairs[pendPair].r, Device::OutDest::DAW);
                        pendPair = -1; ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100,0))) {
                        if (pendPair>=0) routeSel[pendPair] = pendPrev;   // revert radio
                        pendPair = -1; ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::Separator();
            // Digital I/O optical mode (0 = S/PDIF, 1 = ADAT in the radios)
            static int digIn = 0, digOut = 0; static double digPoll = 0;
            if (connected) { double tt = now_ms(); if (tt - digPoll > 1000) { digPoll = tt;
                bool a; if (dev.getDigitalInputADAT(a)) digIn = a?1:0;
                if (dev.getDigitalOutputADAT(a)) digOut = a?1:0; } }
            ImGui::TextDisabled("Digital In: "); ImGui::SameLine();
            if (ImGui::RadioButton("S/PDIF##i", &digIn, 0) && connected) dev.setDigitalInputADAT(false); ImGui::SameLine();
            if (ImGui::RadioButton("ADAT##i", &digIn, 1) && connected) dev.setDigitalInputADAT(true);
            ImGui::TextDisabled("Digital Out:"); ImGui::SameLine();
            if (ImGui::RadioButton("S/PDIF##o", &digOut, 0) && connected) dev.setDigitalOutputADAT(false); ImGui::SameLine();
            if (ImGui::RadioButton("ADAT##o", &digOut, 1) && connected) dev.setDigitalOutputADAT(true);
            ImGui::Separator();
            // Sample rate
            ImGui::TextDisabled("Sample rate:"); ImGui::SameLine();
            const int rates[4] = {44100, 48000, 88200, 96000};
            for (int r : rates) { char b[12]; snprintf(b,sizeof b,"%d", r/1000); ImGui::SameLine();
                if (ImGui::Button(b) && connected) dev.setSampleRate(r); }
            ImGui::Separator();
            // Mono mode
            ImGui::TextDisabled("Mono mode:"); ImGui::SameLine();
            static int mm = 0; const char* mmn[3] = {"L","C","R"};
            for (int k=0;k<3;k++){ ImGui::SameLine(); if (ImGui::RadioButton(mmn[k], &mm, k) && connected) dev.setMonoMode((MonoMode)k); }
            ImGui::Separator();
            // Clock source (ALSA enum) — slave to optical when feeding S/PDIF/ADAT in
            static int clk = 0; static bool optValid = false; static double clkPoll = 0;
            if (connected) { double tt = now_ms(); if (tt - clkPoll > 1000) { clkPoll = tt;
                int cs = dev.getClockSource(); if (cs >= 0) clk = cs; optValid = dev.getOpticalClockValid(); } }
            ImGui::TextDisabled("Clock source:"); ImGui::SameLine();
            const char* clkn[2] = {"Internal","Optical (S/PDIF/ADAT)"};
            for (int k=0;k<2;k++){ ImGui::SameLine(); if (ImGui::RadioButton(clkn[k], &clk, k) && connected) dev.setClockSource(k); }
            ImGui::SameLine(); ImGui::TextDisabled(optValid ? "[optical: locked]" : "[optical: no signal]");
            // Warn if a digital clock is present but we're not slaving to it (crackle!)
            if (optValid && clk == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.6f,0.2f,1));
                ImGui::TextWrapped("\xE2\x9A\xA0 Optical input detected but clock = Internal. This causes "
                                   "crackling. Set Clock source to Optical to slave to it.");
                ImGui::PopStyleColor();
                if (ImGui::Button("Fix: slave to Optical") && connected) { dev.setClockSource(1); clk = 1; }
            }
            ImGui::Separator();
            // Trim knobs
            static float dimTrim = 0.5f, altTrim = 0.5f;
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderFloat("Dim trim", &dimTrim, 0, 1, "%.2f") && connected) dev.setDimTrim(dimTrim);
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderFloat("Alt trim", &altTrim, 0, 1, "%.2f") && connected) dev.setAltTrim(altTrim);
            ImGui::End();
        }

        // ===== Virtual Devices: live Windows-style stereo split via pw-loopback =====
        if (showDevices) {
            ImGui::SetNextWindowSize(ImVec2(430, 380), ImGuiCond_FirstUseEver);
            ImGui::Begin("Virtual Devices", &showDevices);
            if (!pwsplit::pwAvailable()) {
                ImGui::TextWrapped("pw-loopback not found. Install your distro's PipeWire "
                                   "tools (e.g. 'pipewire' / 'pipewire-audio') to use this.");
            } else {
                auto& dv = pwsplit::devices();
                static std::vector<char> run(dv.size(), 0);
                static double poll = 0; double tt = now_ms();
                if (tt - poll > 1500) { poll = tt; for (size_t i=0;i<dv.size();i++) run[i] = pwsplit::running(dv[i].node); }
                ImGui::TextWrapped("Split the iD's single 16-channel device into clean stereo "
                                   "sinks/sources, like the Windows driver. Fixes games that "
                                   "scatter audio across channels your monitors don't carry.");
                ImGui::Dummy(ImVec2(0,4));
                if (ImGui::Button("Enable inputs (Duplex profile)")) pwsplit::setDuplex();
                ImGui::SameLine(); ImGui::TextDisabled("(for Mic/ADAT)");
                ImGui::Separator();
                ImGui::TextDisabled("OUTPUTS");
                for (size_t i=0;i<dv.size();i++) {
                    if (!dv[i].sink && (i==0 || dv[i-1].sink)) { ImGui::Separator(); ImGui::TextDisabled("INPUTS"); }
                    bool on = run[i];
                    ImGui::PushID((int)i);
                    if (ImGui::Checkbox(dv[i].label, &on)) {
                        if (on) { if (!pwsplit::enable(dv[i])) on=false; } else pwsplit::disable(dv[i]);
                        run[i] = on; poll = 0;
                    }
                    ImGui::PopID();
                }
                ImGui::Separator();
                static std::string savedPath; static double savedAt = 0;
                if (ImGui::Button("Save as startup config")) {
                    bool en[16]={false}; for (size_t i=0;i<dv.size() && i<16;i++) en[i]=run[i];
                    if (pwsplit::saveConfig(en, savedPath)) savedAt = now_ms();
                }
                ImGui::SameLine(); ImGui::TextDisabled("(persists across reboots)");
                if (savedAt && now_ms()-savedAt < 8000)
                    ImGui::TextWrapped("Saved to %s", savedPath.c_str());
            }
            ImGui::End();
        }

        ImGui::End();
        ImGui::Render();
        int w,h; glfwGetFramebufferSize(win,&w,&h); glViewport(0,0,w,h);
        glClearColor(monixtheme::kBg.x,monixtheme::kBg.y,monixtheme::kBg.z,1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);

        { static double ls=0; MonixState c=snap(); double t=now_ms();
          if (t-ls>800){ saveState(c); ls=t; } }
    }
    saveState(snap());
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}
