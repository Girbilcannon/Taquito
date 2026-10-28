#pragma once
#include "../Core/MarkerPack.h"
#include "../mumble/Mumble.h"

namespace MarkerInteractions {
void RequestInteract();
void Reset();
void CopyCategory(const std::string& text,const std::string& message);
void Render(const std::vector<TacoPack::Pack>& packs,const Mumble::Data* mumble,bool enabled,void (*save)()=nullptr,void (*categoryChanged)()=nullptr);
}
