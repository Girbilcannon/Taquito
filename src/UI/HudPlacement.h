#pragma once
#include <algorithm>
#include <cmath>
namespace HudPlacement {
struct Bounds {float x,y,width,height;};
inline Bounds Rescue(float x,float y,float width,float height,float screenWidth,float screenHeight) {
    screenWidth=std::isfinite(screenWidth)?std::max(0.0f,screenWidth):0;
    screenHeight=std::isfinite(screenHeight)?std::max(0.0f,screenHeight):0;
    width=std::isfinite(width)?std::clamp(width,0.0f,screenWidth):screenWidth;
    height=std::isfinite(height)?std::clamp(height,0.0f,screenHeight):screenHeight;
    x=std::isfinite(x)?std::clamp(x,0.0f,screenWidth-width):0;
    y=std::isfinite(y)?std::clamp(y,0.0f,screenHeight-height):0;
    return {x,y,width,height};
}
}
