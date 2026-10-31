#pragma once
#include "../Core/MarkerPack.h"
namespace PresetsWindow {
void Open();void Close();
void Render(std::vector<TacoPack::Pack>&packs,void(*changed)());
}
