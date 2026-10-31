#include "AttributeRuntime.h"
#include "AtomicFile.h"
#include "CheckpointRuntime.h"
#include "../third_party/jsonxx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <unordered_set>

namespace {
uint32_t currentMap=0,currentShard=0,currentInstance=0;
int currentMount=-1;
std::string currentCharacter,status;
double utc=0,clockSeconds=0;
std::filesystem::path stateFile;
std::unordered_map<std::string,double> timers;
unsigned batchDepth=0;bool persistenceDirty=false;
std::unordered_set<std::string> mapState,instanceState;
struct Animation { bool focused=false;double start=0,returnStart=0;float value=0,returnValue=0; };
std::unordered_map<const TacoPack::Marker*,Animation> animations;
std::string LowerTrim(std::string text) {
    auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    text=text.substr(first,last-first+1);
    for(char& c:text)if(c>='A'&&c<='Z')c+=('a'-'A');return text;
}
std::string TimerKey(const TacoPack::Marker& marker) {
    if(marker.stateKey.empty())return {};
    if(marker.style.behavior==7) {
        if(currentCharacter.empty())return {};
        // Taquito-owned format; deliberately separate from Blish state files.
        return "character:"+std::to_string(currentCharacter.size())+":"+currentCharacter+":"+marker.stateKey;
    }
    return marker.stateKey;
}
std::string InstanceKey(const TacoPack::Marker& marker) {
    return std::to_string(currentMap)+":"+std::to_string(currentShard)+":"+std::to_string(currentInstance)+":"+marker.stateKey;
}
void Save() {
    if(stateFile.empty())return;
    jsonxx::Object data,entries;data.import("schema",jsonxx::Value(1));
    for(auto it=timers.begin();it!=timers.end();) {
        if(it->second>0 && it->second<=utc)it=timers.erase(it);
        else {entries.import(it->first,jsonxx::Value(it->second));++it;}
    }
    data.import("timers",jsonxx::Value(entries));
    status=AtomicFile::Write(stateFile,data.json()) ? "Behavior state saved." : "Could not save behavior state; current session remains usable.";
}
void Persist() {persistenceDirty=true;if(!batchDepth){Save();persistenceDirty=false;}}
bool Activated(const TacoPack::Marker& marker) {
    if(marker.stateKey.empty())return false;
    switch(marker.style.behavior) {
        case 1:return mapState.count(marker.stateKey)!=0;
        case 6:return instanceState.count(InstanceKey(marker))!=0;
        case 2:case 3:case 4:case 7:case 101: {
            auto key=TimerKey(marker);if(key.empty())return false;
            auto it=timers.find(key);return it!=timers.end()&&(it->second==0||it->second>utc);
        }
        default:return false; // Unsupported modes never install a filter.
    }
}
float Ease2(float t) {return t<.5f ? 2*t*t : 1-std::pow(-2*t+2,2.0f)*.5f;}
float Ease4(float t) {return t<.5f ? 8*t*t*t*t : 1-std::pow(-2*t+2,4.0f)*.5f;}
float BounceOut(float t) {
    constexpr float n=7.5625f,d=2.75f;
    if(t<1/d)return n*t*t;
    if(t<2/d){t-=1.5f/d;return n*t*t+.75f;}
    if(t<2.5f/d){t-=2.25f/d;return n*t*t+.9375f;}
    t-=2.625f/d;return n*t*t+.984375f;
}
}
std::string AttributeRuntime::CanonicalGuid(const std::string& text) {
    auto trimmed=text;auto first=trimmed.find_first_not_of(" \t\r\n"),last=trimmed.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    trimmed=trimmed.substr(first,last-first+1);
    auto hex=LowerTrim(trimmed);hex.erase(std::remove_if(hex.begin(),hex.end(),[](char c){return c=='-'||c=='{'||c=='}';}),hex.end());
    if(hex.size()==32 && std::all_of(hex.begin(),hex.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) {
        // .NET/Windows Guid byte order: first three fields are little endian.
        {
            std::string bytes;
            for(int i:{6,4,2,0,10,8,14,12,16,18,20,22,24,26,28,30})bytes+=hex.substr(i,2);
            return bytes;
        }
    }
    constexpr const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(trimmed.size()!=24 && trimmed.size()!=22)return {};
    std::array<int,256> table;table.fill(-1);for(int i=0;i<64;++i)table[static_cast<unsigned char>(alphabet[i])]=i;
    std::string bytes;uint32_t buffer=0;int bits=0;bool padding=false;int pads=0;
    for(unsigned char c:trimmed) {
        if(c=='='){padding=true;++pads;continue;}
        if(padding||table[c]<0)return {};
        buffer=(buffer<<6)|table[c];bits+=6;
        if(bits>=8){bits-=8;bytes+=static_cast<char>((buffer>>bits)&255u);}
    }
    if(bytes.size()!=16 || (trimmed.size()==24 && pads!=2) || (buffer&15u))return {};
    std::string result;constexpr const char* digits="0123456789abcdef";
    for(unsigned char c:bytes){result+=digits[c>>4];result+=digits[c&15];}return result;
}
void AttributeRuntime::Initialize(const std::filesystem::path& directory) {
    stateFile=directory/"behavior-state.json";timers.clear();mapState.clear();instanceState.clear();animations.clear();
    batchDepth=0;persistenceDirty=false;
    currentMap=0;currentCharacter.clear();utc=0;clockSeconds=0;currentMount=-1;
    status="Behavior state ready.";
    std::error_code ec;const auto size=std::filesystem::file_size(stateFile,ec);if(ec)return;
    if(size>8*1024*1024){status="Behavior state exceeds size limit; ignored.";return;}
    std::ifstream input(stateFile);jsonxx::Object data;
    if(!data.parse(input)||!data.has<jsonxx::Number>("schema")||data.get<jsonxx::Number>("schema")!=1||!data.has<jsonxx::Object>("timers")) {
        auto backup=stateFile;backup+=".invalid";
        std::filesystem::copy_file(stateFile,backup,std::filesystem::copy_options::overwrite_existing,ec);
        status="Malformed behavior state ignored; backup attempted as behavior-state.json.invalid.";return;
    }
    for(const auto& [key,value]:data.get<jsonxx::Object>("timers").kv_map()) {
        if(timers.size()>=100000)break;
        if(key.size()<=512&&value->is<jsonxx::Number>()) {
            double n=static_cast<double>(value->get<jsonxx::Number>());if(std::isfinite(n)&&n>=0)timers[key]=n;
        }
    }
}
void AttributeRuntime::Tick(uint32_t map,uint32_t shard,uint32_t instance,const std::string& character,int mount,double now,double animation) {
    if(map && currentMap!=map){mapState.clear();animations.clear();currentMap=map;}
    if(map){currentShard=shard;currentInstance=instance;}
    currentCharacter=character;currentMount=map?mount:-1;
    if(std::isfinite(now)&&now>=0)utc=now;
    if(std::isfinite(animation))clockSeconds=animation;
}
double AttributeRuntime::NextDaily(double now) {return (std::floor(now/86400)+1)*86400;}
double AttributeRuntime::NextWeekly(double now) {
    auto days=static_cast<int64_t>(std::floor(now/86400));int weekday=static_cast<int>((days+3)%7);if(weekday<0)weekday+=7;
    double target=static_cast<double>(days-weekday)*86400+7.5*3600;
    if(target<=now)target+=7*86400;return target;
}
bool AttributeRuntime::MountAllowed(const TacoPack::Style& style) {
    return !style.mountRestricted||(currentMount>=0&&currentMount<11&&(style.mounts&(1u<<currentMount)));
}
bool AttributeRuntime::Visible(const TacoPack::Marker& marker) {
    if(!TacoPack::IsVisible(marker.category)||!MountAllowed(marker.style)||!CheckpointRuntime::Visible(marker))return false;
    if(marker.style.behavior<=0 || !TacoPack::SupportedBehavior(marker.style.behavior))return true;
    return Activated(marker)==marker.style.invertBehavior;
}
bool AttributeRuntime::Visible(const TacoPack::Trail& trail) {return TacoPack::IsVisible(trail.category)&&MountAllowed(trail.style)&&CheckpointRuntime::Visible(trail);}
bool AttributeRuntime::HasLoadedResetTarget(const TacoPack::Marker&marker,const std::vector<TacoPack::Pack>&packs) {
    for(const auto&pack:packs)for(const auto&target:pack.markers)
        if(!target.stateKey.empty()&&std::find(marker.style.resetGuids.begin(),marker.style.resetGuids.end(),target.stateKey)!=marker.style.resetGuids.end())return true;
    return false;
}
bool AttributeRuntime::Activate(const TacoPack::Marker& marker,const std::vector<TacoPack::Pack>& packs) {
    bool changed=false;
    const auto applyCategory=[&](const std::string& text,int action) {
        const auto name=LowerTrim(text);if(name.empty())return;
        for(const auto& pack:packs) {
            auto it=pack.categories.find(name);if(it==pack.categories.end())continue;
            bool next=action<0 ? !it->second->visible : action!=0;
            if(next!=it->second->visible){it->second->visible=next;changed=true;}
        }
    };
    applyCategory(marker.style.toggleCategory,-1);applyCategory(marker.style.showCategory,1);applyCategory(marker.style.hideCategory,0);
    bool persistent=false;
    if(!marker.stateKey.empty()) switch(marker.style.behavior) {
        case 1:mapState.insert(marker.stateKey);break;
        case 6:instanceState.insert(InstanceKey(marker));break;
        case 2:case 3:case 4:case 7:case 101: {
            auto key=TimerKey(marker);if(key.empty())break;
            if(marker.style.behavior==4 && marker.style.resetLength<=0) {timers.erase(key);persistent=true;break;}
            double expiry=marker.style.behavior==3 ? 0 : marker.style.behavior==4 ? utc+marker.style.resetLength :
                marker.style.behavior==101 ? NextWeekly(utc) : NextDaily(utc);
            timers[key]=expiry;persistent=true;break;
        }
    }
    // Own ordinary behavior precedes explicit reset, including self-targeting.
    BeginActivationBatch();
    if(persistent)Persist();
    if(!marker.style.resetGuids.empty()) {
        std::unordered_set<std::string> requested(marker.style.resetGuids.begin(),marker.style.resetGuids.end()),resolved;
        for(const auto&pack:packs)for(const auto&target:pack.markers)
            if(!target.stateKey.empty()&&requested.count(target.stateKey))resolved.insert(target.stateKey);
        for(const auto&guid:resolved)ClearBehaviorState(guid);
    }
    EndActivationBatch();return changed;
}
bool AttributeRuntime::ClearBehaviorState(const std::string& guid) {
    if(guid.empty())return false;
    bool changed=mapState.erase(guid)!=0;
    for(auto it=instanceState.begin();it!=instanceState.end();) {
        const auto colon=it->rfind(':');
        if(colon!=std::string::npos&&it->substr(colon+1)==guid){it=instanceState.erase(it);changed=true;}else ++it;
    }
    bool persistent=timers.erase(guid)!=0;
    if(!currentCharacter.empty())persistent|=timers.erase("character:"+std::to_string(currentCharacter.size())+":"+currentCharacter+":"+guid)!=0;
    if(persistent)Persist();return changed||persistent;
}
void AttributeRuntime::BeginActivationBatch(){++batchDepth;}
void AttributeRuntime::EndActivationBatch(){if(batchDepth&&!--batchDepth&&persistenceDirty){Save();persistenceDirty=false;}}
void AttributeRuntime::ResetFocus(){animations.clear();}
void AttributeRuntime::ResetBehavior(){timers.clear();mapState.clear();instanceState.clear();Persist();}
std::string AttributeRuntime::Status(){return status;}
void AttributeRuntime::UpdateFocus(const std::vector<TacoPack::Pack>& packs,TacoPack::Vec3 player,bool active) {
    if(!active){animations.clear();return;}
    std::unordered_set<const TacoPack::Marker*> seen;
    for(const auto& pack:packs)for(const auto& marker:pack.markers) {
        const auto& s=marker.style;if(marker.map!=currentMap||s.bounce==TacoPack::Style::Bounce::None)continue;
        auto delta=TacoPack::Vec3{marker.position.x-player.x,marker.position.y-player.y,marker.position.z-player.z};
        double distance=static_cast<double>(delta.x)*delta.x+static_cast<double>(delta.y)*delta.y+static_cast<double>(delta.z)*delta.z;
        bool focused=Visible(marker)&&std::isfinite(distance)&&distance<=static_cast<double>(s.triggerRange)*s.triggerRange;
        auto found=animations.find(&marker);if(!focused&&found==animations.end())continue;
        auto& a=animations[&marker];seen.insert(&marker);
        if(focused&&!a.focused){a.focused=true;a.start=clockSeconds;}
        if(!focused&&a.focused){a.focused=false;a.returnStart=clockSeconds;a.returnValue=a.value;}
        if(a.focused) {
            double elapsed=clockSeconds-a.start-s.bounceDelay;
            if(elapsed<=0)a.value=0;
            else if(s.bounce==TacoPack::Style::Bounce::Rise)a.value=s.bounceHeight*Ease4(static_cast<float>(std::min(1.0,elapsed/s.bounceDuration)));
            else {float t=static_cast<float>(std::fmod(elapsed/s.bounceDuration,2.0));if(t>1)t=2-t;a.value=s.bounceHeight*Ease2(t);}
        } else {
            float t=static_cast<float>(std::clamp((clockSeconds-a.returnStart)/s.bounceDuration,0.0,1.0));
            a.value=a.returnValue*(1-BounceOut(t));if(t>=1)seen.erase(&marker);
        }
    }
    for(auto it=animations.begin();it!=animations.end();)if(!seen.count(it->first))it=animations.erase(it);else ++it;
}
float AttributeRuntime::BounceOffset(const TacoPack::Marker& marker) {auto it=animations.find(&marker);return it==animations.end()?0:it->second.value;}
TacoPack::Vec3 AttributeRuntime::RibbonSide(TacoPack::Vec3 d,bool wall) {
    const auto cross=[](TacoPack::Vec3 a,TacoPack::Vec3 b){return TacoPack::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    TacoPack::Vec3 side=wall ? cross(d,cross(d,{0,1,0})) : cross(d,{0,1,0});
    double length=std::hypot(std::hypot(side.x,side.y),side.z);
    if(!std::isfinite(length)||length<1e-7)return {1,0,0};
    return {static_cast<float>(side.x/length),static_cast<float>(side.y/length),static_cast<float>(side.z/length)};
}
