#pragma once
#include <vector>
#include "../Core/MarkerPack.h"

class Renderer;
namespace SettingsWindow {
void Open();
void Close();
void Render(const std::vector<TacoPack::Pack>& packs, const Renderer& renderer,
            bool loading, void (*reload)(), void (*save)());
}
