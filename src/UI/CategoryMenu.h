#pragma once
#include "../Core/AppSettings.h"

namespace CategoryMenu {
// Both layouts edit the same category visibility and use the same save callback.
void Draw(TacoPack::Category& category, AppSettings::NestingStyle style, void (*changed)());
}
