#pragma once
#include "AttributeRuntime.h"
#include <charconv>
#include <cmath>
#include <unordered_set>
namespace CheckpointAttributes {
inline std::string Trim(const std::string& text) {
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    return first==std::string::npos ? std::string{} : text.substr(first,last-first+1);
}
inline std::vector<std::string> Guids(const std::string& text,bool& invalid) {
    std::vector<std::string> result;std::unordered_set<std::string> seen;size_t start=0;
    if(Trim(text).empty())return result;
    do {
        auto end=text.find(',',start);auto part=Trim(text.substr(start,end==std::string::npos ? end : end-start));
        const auto key=AttributeRuntime::CanonicalGuid(part);
        if(key.empty())invalid=true;else if(seen.insert(key).second)result.push_back(key);
        if(end==std::string::npos)break;start=end+1;
    }while(start<=text.size());return result;
}
inline bool Number(const std::string& text,float& result) {
    auto value=Trim(text);try {size_t used=0;result=std::stof(value,&used);return used==value.size()&&std::isfinite(result);}catch(...){return false;}
}
inline void Populate(TacoPack::Style& style) {
    auto get=[&](const char* key)->const std::string* {auto it=style.attributes.find(key);return it==style.attributes.end()?nullptr:&it->second;};
    auto flag=[&](const char* key) {auto*v=get(key);if(!v)return false;auto t=Trim(*v);for(auto&c:t)if(c>='A'&&c<='Z')c+=('a'-'A');return t=="true"||t=="1";};
    style.cpFinish=flag("cpfinish");style.resetMessage=get("resetmessage")?*get("resetmessage"):std::string{};
    style.checkpoint=flag("ischeckpoint");style.checkpointErrors=0;
    style.resetGuids.clear();if(auto*text=get("resetguid")){bool invalid=false;style.resetGuids=Guids(*text,invalid);if(invalid)style.checkpointErrors|=1;}
    style.cpReset.clear();style.cpResetConfigured=false;
    if(auto*text=get("cpreset")) {style.cpResetConfigured=true;style.cpReset=AttributeRuntime::CanonicalGuid(*text);if(style.cpReset.empty())style.checkpointErrors|=2;}
    float range=0;
    const auto*trigger=get("triggerrange");
    const bool explicitRange=trigger&&Number(*trigger,range)&&range>=0;
    style.cpResetActivationAllowed=style.autoTrigger || explicitRange;
    if(!style.cpReset.empty()&&style.autoTrigger&&!explicitRange)style.triggerRange=2.0f;
    if(!style.cpReset.empty()&&!style.cpResetActivationAllowed)style.checkpointErrors|=4;
    style.cpColor=style.cpPreColor=0xffffffffu;style.cpRadius=2;style.nextCheckCount=2;style.nextChecks.clear();
    if(!style.checkpoint&&!style.cpFinish)return; // Dedicated reset markers do not need native visual options.
    const auto color=[&](const char*key,uint32_t&target,uint32_t error){
        if(auto*value=get(key)) {
            auto text=Trim(*value);uint32_t parsed=0;auto r=std::from_chars(text.data(),text.data()+text.size(),parsed,16);
            if(text.size()!=8||r.ec!=std::errc{}||r.ptr!=text.data()+text.size())style.checkpointErrors|=error;
            else target=parsed; // Native checkpoint colors are RRGGBBAA, never ARGB.
        }
    };
    color("cpcolor",style.cpColor,8);color("cpprecolor",style.cpPreColor,16);
    if(auto*value=get("cpradius")){float radius=0;if(Number(*value,radius)&&radius>0)style.cpRadius=radius;else style.checkpointErrors|=32;}
    if(auto*value=get("nextcheckcount")){
        auto text=Trim(*value);uint32_t count=0;auto r=std::from_chars(text.data(),text.data()+text.size(),count);
        if(text.empty()||r.ec!=std::errc{}||r.ptr!=text.data()+text.size())style.checkpointErrors|=64;else style.nextCheckCount=count;
    }
    if(auto*value=get("nextcheck")){bool invalid=false;style.nextChecks=Guids(*value,invalid);if(invalid)style.checkpointErrors|=128;}
}
}
