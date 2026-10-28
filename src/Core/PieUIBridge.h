#pragma once
#include <string>
namespace PieUIBridge {
inline constexpr const char* PingEvent="EV_PIE_UI_PING";
inline constexpr const char* ReadyEvent="EV_PIE_UI_READY";
inline constexpr const char* OpenEvent="EV_PIEUI_OPEN_CHATLINK";
using RaiseEvent=void (*)(const char*,void*);
// Initialize and subscribe to ReadyEvent before probing. Poll/dispatch run on the UI thread.
void Initialize(RaiseEvent raise);
void Shutdown();
void OnReady(void*);
void Probe();
void Poll(double frameSeconds);
bool Present();
// A single complete base64 type-0x04 map link; whitespace outside the link is allowed.
// This verifies format, not whether a particular map point exists or is unlocked.
std::string MapLink(const std::string& text);
bool CanOpenMap(const std::string& text);
// Rechecks presence immediately; the borrowed string is valid throughout synchronous dispatch.
bool OpenMap(const std::string& text);
}
