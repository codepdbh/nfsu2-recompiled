#pragma once
#include <string>
// Maps the owner's PE data, binds imports, tests lifted code and attempts CRT
// entry until the first unsupported host service. Returns an honest status.
std::string connectGuestRuntime(const char* executable);
void requestGuestRuntimeStop();
void setGuestPaused(bool paused);
void setGuestDisplaySize(unsigned width,unsigned height);
void setGuestKey(unsigned scan,bool down);
void clearGuestInput();
bool guestDrivingControls();
// Launcher option; only before the guest boots.
void setGuestWidescreen(bool enabled);
void setGuestLanguage(const char* language);
void setGuestResolution(unsigned width,unsigned height);
void setGuestFrameLimit(unsigned framesPerSecond);
