#pragma once
#include "imgui.h"

// Monix dark-violet theme.
namespace monixtheme {

// palette
inline const ImVec4 kBg       = ImVec4(0.090f, 0.082f, 0.110f, 1.0f); // #17151c
inline const ImVec4 kPanel    = ImVec4(0.130f, 0.120f, 0.155f, 1.0f);
inline const ImVec4 kPanel2   = ImVec4(0.165f, 0.150f, 0.195f, 1.0f);
inline const ImVec4 kText     = ImVec4(0.925f, 0.910f, 0.940f, 1.0f); // #ece8f0
inline const ImVec4 kTextDim  = ImVec4(0.62f,  0.60f,  0.66f,  1.0f);
inline const ImVec4 kAccent   = ImVec4(0.655f, 0.545f, 0.980f, 1.0f); // #a78bfa violet
inline const ImVec4 kAccentHi = ImVec4(0.745f, 0.655f, 1.000f, 1.0f);

void apply();                       // install colors/style
ImU32 meterColor(float level01);    // violet -> amber -> red

} // namespace monixtheme
