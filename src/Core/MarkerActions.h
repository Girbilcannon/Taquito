#pragma once
#include "MarkerPack.h"
#include "CheckpointAttributes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <charconv>
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
    auto boolean=[&](const char* name,bool fallback) {
        auto text=value(name);text.erase(0,text.find_first_not_of(" \t\r\n"));
        const auto end=text.find_last_not_of(" \t\r\n");if(end!=std::string::npos)text.resize(end+1);
        for(auto& c:text)if(c>='A'&&c<='Z')c+=('a'-'A');
        return text=="1"||text=="true" ? true : text=="0"||text=="false" ? false : fallback;
    };
    style.showCategory=value("show");style.hideCategory=value("hide");
    // An empty TacO alias explicitly overrides an inherited/nonempty toggle.
    style.toggleCategory=style.attributes.count("togglecategory") ? value("togglecategory") : value("toggle");
    const auto behavior=value("behavior");
    // Require an integer ID; don't round a fractional value into a supported mode.
    const auto firstBehavior=behavior.find_first_not_of(" \t\r\n"),lastBehavior=behavior.find_last_not_of(" \t\r\n");
    int mode=0;
    if(firstBehavior!=std::string::npos) {
        const auto* end=behavior.data()+lastBehavior+1;
        const auto parsed=std::from_chars(behavior.data()+firstBehavior,end,mode);
        if(parsed.ec!=std::errc{} || parsed.ptr!=end)mode=0;
    }
    style.behavior=TacoPack::SupportedBehavior(mode) ? mode : 0;
    style.invertBehavior=boolean("invertbehavior",false);
    style.canFade=boolean("canfade",true);style.isWall=boolean("iswall",false);
    style.resetLength=std::clamp(number("resetlength",0),0.0f,315576000.0f);
    auto bounce=value("bounce");for(auto& c:bounce)if(c>='A'&&c<='Z')c+=('a'-'A');
    style.bounce=bounce=="bounce" ? TacoPack::Style::Bounce::Bounce : bounce=="rise" ? TacoPack::Style::Bounce::Rise : TacoPack::Style::Bounce::None;
    style.bounceHeight=std::clamp(number("bounce-height",2),-1000.0f,1000.0f);
    style.bounceDuration=std::clamp(number("bounce-duration",1),0.001f,3600.0f);
    style.bounceDelay=std::clamp(number("bounce-delay",0),0.0f,3600.0f);
    style.mountRestricted=style.attributes.count("mount")!=0;style.mounts=0;
    const char* names[]={"none","jackal","griffon","springer","skimmer","raptor","rollerbeetle","warclaw","skyscale","skiff","siegeturtle"};
    auto mounts=value("mount");size_t start=0;
    do {
        auto end=mounts.find(',',start);auto name=mounts.substr(start,end==std::string::npos ? end : end-start);
        auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
        name=first==std::string::npos ? "" : name.substr(first,last-first+1);
        for(auto& c:name)if(c>='A'&&c<='Z')c+=('a'-'A');
        for(int i=0;i<11;++i)if(name==names[i])style.mounts|=static_cast<uint16_t>(1u<<i);
        if(end==std::string::npos)break;start=end+1;
    } while(start<=mounts.size());
    style.info=value("info");style.copy=value("copy");style.copyMessage=value("copy-message");
    style.triggerRange=std::max(0.0f,number("triggerrange",2.0f));
    // Info text has its own radius; absent inforange falls back to triggerRange.
    style.infoRange=std::max(0.0f,number("inforange",style.triggerRange));
    auto automatic=value("autotrigger");
    const auto first=automatic.find_first_not_of(" \t\r\n"),last=automatic.find_last_not_of(" \t\r\n");
    if(first!=std::string::npos) automatic=automatic.substr(first,last-first+1);
    for(auto& c:automatic) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    style.autoTrigger=automatic=="1" || automatic=="true";
    CheckpointAttributes::Populate(style);
}
inline bool Actionable(const TacoPack::Style& s) {
    return !s.resetGuids.empty() || (!s.cpReset.empty() && s.cpResetActivationAllowed) || !s.copy.empty() || !s.showCategory.empty() || !s.hideCategory.empty() || !s.toggleCategory.empty() ||
        s.behavior==1 || s.behavior==2 || s.behavior==3 || s.behavior==4 || s.behavior==6 || s.behavior==7 || s.behavior==101;
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
    const TacoPack::Marker* action=nullptr;
    std::vector<const TacoPack::Marker*> entered;
};
class Proximity {
    std::unordered_set<const TacoPack::Marker*> inside_;
    std::unordered_set<const TacoPack::Marker*> infoInside_;
    uint32_t map_=0;
    bool baseline_=false;
public:
    void Reset() {inside_.clear();infoInside_.clear();map_=0;baseline_=false;}
    bool IsInfoInside(const TacoPack::Marker* marker) const {return infoInside_.count(marker)!=0;}
    Selection Update(const std::vector<TacoPack::Pack>& packs,uint32_t map,TacoPack::Vec3 player,bool active,bool (*visible)(const TacoPack::Marker&)=nullptr) {
        Selection selected;
        if(!active || !map) {Reset();return selected;}
        if(map_!=map) {Reset();map_=map;}
        std::unordered_set<const TacoPack::Marker*> inside;
        std::unordered_set<const TacoPack::Marker*> infoInside;
        double infoDistance=std::numeric_limits<double>::infinity(),copyDistance=infoDistance,autoDistance=infoDistance,actionDistance=infoDistance;
        for(const auto& pack:packs) for(const auto& marker:pack.markers) {
            if(marker.map!=map || (marker.style.info.empty() && !Actionable(marker.style)) || !TacoPack::IsVisible(marker.category) || (visible && !visible(marker))) continue;
            const double x=static_cast<double>(marker.position.x)-player.x,y=static_cast<double>(marker.position.y)-player.y,z=static_cast<double>(marker.position.z)-player.z;
            const double distance=x*x+y*y+z*z,range=marker.style.triggerRange,infoRange=marker.style.infoRange;
            if(!std::isfinite(distance)) continue;
            if(!marker.style.info.empty() && std::isfinite(infoRange) && infoRange>=0 && distance<=infoRange*infoRange) {
                infoInside.insert(&marker);
                if(distance<infoDistance) {selected.info=&marker;infoDistance=distance;}
            }
            if(Actionable(marker.style) && std::isfinite(range) && range>=0 && distance<=range*range) {
                inside.insert(&marker);
                if(distance<actionDistance) {selected.action=&marker;actionDistance=distance;}
                if(!marker.style.copy.empty() && distance<copyDistance) {selected.copy=&marker;copyDistance=distance;}
                if(marker.style.autoTrigger && baseline_ && !inside_.count(&marker) && distance<autoDistance) {
                    selected.automatic=&marker;autoDistance=distance;
                }
                if(marker.style.autoTrigger && baseline_ && !inside_.count(&marker)) selected.entered.push_back(&marker);
            }
        }
        baseline_=true;
        inside_=std::move(inside);
        infoInside_=std::move(infoInside);
        return selected;
    }
};
}
