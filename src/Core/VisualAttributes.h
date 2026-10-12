#pragma once
#include "MarkerPack.h"
#include <algorithm>
#include <cmath>

namespace VisualAttributes {
inline float MapTrailWidth(float displaySize,float trailScale) {
    return std::clamp(displaySize*.5f*trailScale*2.5f,1.0f,100.0f);
}
inline float Opacity(const TacoPack::Style& style, float meters) {
    const float alpha=std::clamp(style.alpha,0.0f,1.0f);
    if(style.fadeNear<0 || style.fadeFar<0) return alpha;
    const float inches=meters/.0254f;
    if(style.fadeFar<=style.fadeNear) return inches<style.fadeFar ? alpha : 0.0f;
    return alpha*std::clamp((style.fadeFar-inches)/(style.fadeFar-style.fadeNear),0.0f,1.0f);
}
inline double TileLength(float width,int imageWidth,int imageHeight,double minimum) {
    return std::max(minimum,imageWidth>0 && imageHeight>0 ? static_cast<double>(width)*imageHeight/imageWidth : static_cast<double>(width));
}
inline double AnimationOffset(double seconds,float speed,double tileLength) {
    return -seconds*speed*tileLength; // speed is texture repeats per second.
}
inline TacoPack::Vec3 Rotate(TacoPack::Vec3 p,TacoPack::Vec3 degrees) {
    constexpr float radians=3.14159265358979323846f/180.0f;
    const float cx=std::cos(degrees.x*radians),sx=std::sin(degrees.x*radians);
    const float cy=std::cos(degrees.y*radians),sy=std::sin(degrees.y*radians);
    const float cz=std::cos(degrees.z*radians),sz=std::sin(degrees.z*radians);
    p={p.x,p.y*cx-p.z*sx,p.y*sx+p.z*cx};
    p={p.x*cy+p.z*sy,p.y,-p.x*sy+p.z*cy};
    return {p.x*cz-p.y*sz,p.x*sz+p.y*cz,p.z};
}
}
