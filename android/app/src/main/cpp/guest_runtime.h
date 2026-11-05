#pragma once
#include <string>
#include <vector>
// Maps the owner's PE data, binds imports, tests lifted code and attempts CRT
// entry until the first unsupported host service. Returns an honest status.
std::string connectGuestRuntime(const char* executable);
void requestGuestRuntimeStop();
void setGuestPaused(bool paused);
void setGuestDisplaySize(unsigned width,unsigned height);
void setGuestKey(unsigned scan,bool down);
void clearGuestInput();
bool guestDrivingControls();
// Physical gamepad seen by the game as a DirectInput joystick: axes X, Y, Z
// (triggers), Rx, Ry, Rz in -1..1; buttons A B X Y LB RB Back Start LS RS as bits.
void setGuestPad(const float axes[6],uint32_t buttons,int hatX,int hatY);
void setGuestPadConnected(bool connected);
// True while the game has the joystick acquired (then the pad stops emulating keys).
bool guestGamepadInUse();
// Launcher option; only before the guest boots.
void setGuestWidescreen(bool enabled);
void setGuestOnlineServer(const std::string& host);
/** Touch as mouse: x/y as a fraction of the game view; phase 0 down, 1 move, 2 up (left button). */
void setGuestPointer(float x,float y,int phase);
/** Posts WM_CHAR messages (the game's text fields read those); 8 is backspace. */
void typeGuestText(const std::vector<uint32_t>& characters);
void setGuestLanguage(const char* language);
void setGuestResolution(unsigned width,unsigned height);
void setGuestFrameLimit(unsigned framesPerSecond);
// Launcher option, before the guest boots: -1 keeps the game's region, 0 North America, 1 Europe.
void setGuestRegion(int region);
// Presented frames per second and average frame time over the last ~0.5 s (0 before the first frame).
float guestOverlayFps();
float guestOverlayFrameMs();
// Console gamepad layout: which menu screen the game shows (0 other or a dialog
// on top, 1 in-game world map, 2 SMS message, 3 SMS mailbox), refreshed every
// frame; and a request to send PAD_BUTTON1 to that screen on the next frame
// (map: set the GPS; mailbox: delete the message).
int guestMenuScreen();
void requestGuestButton1();
