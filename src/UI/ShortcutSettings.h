#pragma once
#include "../Core/AppSettings.h"
#include <string>
namespace ShortcutSettings {
// Local configurable shortcuts; Nexus owns the existing menu/interaction bindings.
struct Result {bool changed=false;unsigned actions=0;};
Result Poll(bool gameFocused,bool allowActions);
void Draw();
void Reset();
unsigned MouseReleased();
unsigned MousePressed();
std::string Label(const AppSettings::Shortcut& key);
}
