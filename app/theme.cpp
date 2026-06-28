#include "theme.h"

namespace monixtheme {

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f; s.FrameRounding = 6.0f; s.GrabRounding = 6.0f;
    s.ChildRounding = 8.0f; s.PopupRounding = 6.0f; s.ScrollbarRounding = 6.0f;
    s.FrameBorderSize = 0.0f; s.WindowBorderSize = 0.0f;
    s.WindowPadding = ImVec2(14, 14); s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8); s.GrabMinSize = 14.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = kBg;
    c[ImGuiCol_ChildBg]         = kPanel;
    c[ImGuiCol_PopupBg]         = kPanel;
    c[ImGuiCol_Text]            = kText;
    c[ImGuiCol_TextDisabled]    = kTextDim;
    c[ImGuiCol_FrameBg]         = kPanel2;
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.26f, 0.23f, 0.32f, 1.0f);
    c[ImGuiCol_Button]          = kPanel2;
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.26f, 0.40f, 1.0f);
    c[ImGuiCol_ButtonActive]    = kAccent;
    c[ImGuiCol_SliderGrab]      = kAccent;
    c[ImGuiCol_SliderGrabActive]= kAccentHi;
    c[ImGuiCol_CheckMark]       = kAccentHi;
    c[ImGuiCol_Header]          = kPanel2;
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.26f, 0.40f, 1.0f);
    c[ImGuiCol_HeaderActive]    = kAccent;
    c[ImGuiCol_Separator]       = ImVec4(0.25f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_Border]          = ImVec4(0.25f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_TitleBg]         = kBg;
    c[ImGuiCol_TitleBgActive]   = kPanel;
    c[ImGuiCol_ScrollbarBg]     = kBg;
    c[ImGuiCol_ScrollbarGrab]   = kPanel2;
    c[ImGuiCol_TableBorderLight]= ImVec4(0.25f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_TableBorderStrong]=ImVec4(0.32f, 0.29f, 0.38f, 1.0f);
}

static ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, 1.0f);
}

ImU32 meterColor(float l) {
    if (l < 0) l = 0; if (l > 1) l = 1;
    const ImVec4 violet = kAccent;
    const ImVec4 amber  = ImVec4(1.00f, 0.70f, 0.25f, 1.0f);
    const ImVec4 red    = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
    ImVec4 col = (l < 0.7f) ? lerp(violet, amber, l / 0.7f)
                            : lerp(amber, red, (l - 0.7f) / 0.3f);
    return ImGui::ColorConvertFloat4ToU32(col);
}

} // namespace monixtheme
