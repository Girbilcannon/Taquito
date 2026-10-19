#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MapTrailVisuals {
// WIC supplies straight RGBA. Ignore transparent padding and weight visible
// colors by coverage. Cache the result with the decoded texture, not per frame.
inline uint32_t SampleColor(const unsigned char* rgba,size_t pixelCount) {
    uint64_t r=0,g=0,b=0,weight=0;
    for(size_t i=0;i<pixelCount;++i) {
        const auto* p=rgba+i*4;
        weight+=p[3];r+=uint64_t(p[0])*p[3];g+=uint64_t(p[1])*p[3];b+=uint64_t(p[2])*p[3];
    }
    if(!weight) return 0;
    const auto channel=[&](uint64_t sum) { return uint32_t((sum+weight/2)/weight); };
    return (channel(r)<<24)|(channel(g)<<16)|(channel(b)<<8)|255u;
}
inline uint32_t LineColor(uint32_t sample,uint32_t tint) {
    // Like Blish, a non-default XML color replaces the sampled color.
    return tint==0xffffffffu ? sample : tint;
}
inline float LineWidth(float uiScale,float userScale) {
    // Screen pixels: deliberately independent of map zoom and XML size fields.
    return std::max(0.5f,4.0f*uiScale*userScale);
}
}
