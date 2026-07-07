#pragma once

namespace monixtray {

bool init();
void shutdown();
void poll();
bool available();
bool takeShowRequest();
bool takeHideRequest();
bool takeQuitRequest();
void setVisibleState(bool visible);

} // namespace monixtray
