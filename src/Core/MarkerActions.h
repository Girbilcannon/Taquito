#pragma once
#include "MarkerPack.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace MarkerActions {
inline void Populate(TacoPack::Style& style) {
    auto value=[&](const char* name) {
        auto it=style.attributes.find(name);
        return it==style.attributes.end() ? std::string{} : it->second;
    };
    auto number=[&](const char* name,float fallback) {
        const auto text=value(name);
        try {
            size_t end=0;float n=std::stof(text,&end);
            if(text.find_first_not_of(" \t\r\n",end)==std::string::npos && std::isfinite(n)) return n;
        } catch(...) {}
        return fallback;
    };
    style.info=value("info");style.copy=value("copy");style.copyMessage=value("copy-message");
    style.triggerRange=std::max(0.0f,number("triggerrange",2.0f));
    // Info text has its own radius; absent inforange falls back to triggerRange.
    style.infoRange=std::max(0.0f,number("inforange",style.triggerRange));
    auto automatic=value("autotrigger");
    const auto first=automatic.find_first_not_of(" \t\r\n"),last=automatic.find_last_not_of(" \t\r\n");
    if(first!=std::string::npos) automatic=automatic.substr(first,last-first+1);
    for(auto& c:automatic) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    style.autoTrigger=automatic=="1" || automatic=="true";
}
inline std::string Message(const std::string& text,const std::string& custom) {
    const auto pattern=custom.empty() ? "'{0}' copied to clipboard." : custom;
    std::string result;
    for(size_t i=0;i<pattern.size();) {
        if(pattern.compare(i,2,"{{")==0) {result+='{' ;i+=2;}
        else if(pattern.compare(i,2,"}}")==0) {result+='}';i+=2;}
        else if(pattern.compare(i,3,"{0}")==0) {result+=text;i+=3;}
        else result+=pattern[i++];
    }
    return result;
}
struct Selection {
    const TacoPack::Marker* info=nullptr;
    const TacoPack::Marker* copy=nullptr;
    const TacoPack::Marker* automatic=nullptr;
};
class Proximity {
    std::unordered_set<const TacoPack::Marker*> inside_;
    std::unordered_set<const TacoPack::Marker*> infoInside_;
    uint32_t map_=0;
public:
    void Reset() {inside_.clear();infoInside_.clear();map_=0;}
    bool IsInfoInside(const TacoPack::Marker* marker) const {return infoInside_.count(marker)!=0;}
    Selection Update(const std::vector<TacoPack::Pack>& packs,uint32_t map,TacoPack::Vec3 player,bool active) {
        Selection selected;
        if(!active || !map) {Reset();return selected;}
        if(map_!=map) {Reset();map_=map;}
        std::unordered_set<const TacoPack::Marker*> inside;
        std::unordered_set<const TacoPack::Marker*> infoInside;
        double infoDistance=std::numeric_limits<double>::infinity(),copyDistance=infoDistance,autoDistance=infoDistance;
        for(const auto& pack:packs) for(const auto& marker:pack.markers) {
            if(marker.map!=map || (marker.style.info.empty() && marker.style.copy.empty()) || !TacoPack::IsVisible(marker.category)) continue;
            const double x=static_cast<double>(marker.position.x)-player.x,y=static_cast<double>(marker.position.y)-player.y,z=static_cast<double>(marker.position.z)-player.z;
            const double distance=x*x+y*y+z*z,range=marker.style.triggerRange,infoRange=marker.style.infoRange;
            if(!std::isfinite(distance)) continue;
            if(!marker.style.info.empty() && std::isfinite(infoRange) && infoRange>=0 && distance<=infoRange*infoRange) {
                infoInside.insert(&marker);
                if(distance<infoDistance) {selected.info=&marker;infoDistance=distance;}
            }
            if(!marker.style.copy.empty() && std::isfinite(range) && range>=0 && distance<=range*range) {
                inside.insert(&marker);
                if(distance<copyDistance) {selected.copy=&marker;copyDistance=distance;}
                if(marker.style.autoTrigger && !inside_.count(&marker) && distance<autoDistance) {
                    selected.automatic=&marker;autoDistance=distance;
                }
            }
        }
        inside_=std::move(inside);
        infoInside_=std::move(infoInside);
        return selected;
    }
};
}
