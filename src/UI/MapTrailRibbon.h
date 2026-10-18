#pragma once
#include "../imgui/imgui.h"
#include "../Core/VisualAttributes.h"
#include <cmath>

namespace MapTrailRibbon {
// The caller scopes a wrap-V/clamp-U sampler around these commands. One quad
// per path segment repeats the texture without a per-tile CPU loop or budget.
inline void Draw(ImDrawList* draw,ImVec2 a,ImVec2 b,float width,ImU32 color,
                 ImTextureID texture,int imageWidth,int imageHeight,double phase) {
    if(!texture || imageWidth<=0 || imageHeight<=0 || !(width>0) ||
       !std::isfinite(width) || !std::isfinite(phase)) return;
    const float dx=b.x-a.x,dy=b.y-a.y;
    const float length=std::hypot(dx,dy);
    if(!std::isfinite(length) || length<.1f) return;
    const float nx=-dy/length*width*.5f,ny=dx/length*width*.5f;
    const double tile=VisualAttributes::TileLength(width,imageWidth,imageHeight,1.0);
    const double offset=std::fmod(std::fmod(phase,tile)+tile,tile);
    const float v0=static_cast<float>(1.0-offset/tile);
    const float v1=static_cast<float>(v0-length/tile);
    draw->AddImageQuad(texture,{a.x+nx,a.y+ny},{b.x+nx,b.y+ny},
        {b.x-nx,b.y-ny},{a.x-nx,a.y-ny},{0,v0},{0,v1},{1,v1},{1,v0},color);
}
}
