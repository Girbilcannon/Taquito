#include "PathingState.h"
#include "../TaquitoAPI.h"
#include "../third_party/jsonxx.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex mutex;
PathingState::RaiseEvent raiseEvent=nullptr;
std::string lastBody,lastJson;
uint64_t revision=0;
uint64_t raisedEvents=0,receivedRequests=0;
size_t publishedPacks=0;
size_t readablePacks=0;
bool snapshotReady=false;
bool active=false;
std::string Utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
std::string Absolute(const std::filesystem::path& path) {
    std::error_code ec;
    auto result=std::filesystem::absolute(path,ec);
    return Utf8((ec ? path : result).lexically_normal());
}
std::string Encode(const std::string& body) {
    // Keep the uint64 counter exact rather than formatting it as a JSON float.
    return "{\"revision\":"+std::to_string(revision)+","+body.substr(1);
}
void SendSnapshot(bool requested) {
    PathingState::RaiseEvent send=nullptr;std::string payload;
    {
        std::lock_guard lock(mutex);
        if(!raiseEvent) return;
        if(requested) ++receivedRequests;
        send=raiseEvent;payload=lastJson;
        if(!payload.empty()) ++raisedEvents;
    }
    if(!payload.empty()) send(EV_TAQUITO_PATHING_STATE,const_cast<char*>(payload.c_str()));
}
}
void PathingState::Initialize(RaiseEvent raise) {
    std::lock_guard lock(mutex);
    raiseEvent=raise;lastBody.clear();lastJson.clear();revision=0;active=false;
    raisedEvents=receivedRequests=0;publishedPacks=0;snapshotReady=false;
    readablePacks=0;
}
void PathingState::Update(const std::vector<TacoPack::Pack>& packs,const AppSettings::State& settings,
                          const std::filesystem::path& directory,const std::vector<InstalledPacks::Record>& catalog,bool ready) {
    jsonxx::Object root;
    root.import("schema",jsonxx::Value(1));
    root.import("addon",jsonxx::Value("Taquito"));
    root.import("version",jsonxx::Value(TAQUITO_VERSION));
    root.import("active",jsonxx::Value(true));
    root.import("ready",jsonxx::Value(ready));
    root.import("renderingEnabled",jsonxx::Value(settings.renderingEnabled));
    root.import("packsDir",jsonxx::Value(Absolute(directory)));
    // Native rendering switches stay local; consumers control their own map widgets.
    // Optional schema-1 extension: dimensionless user multipliers, independent of visibility.
    const auto multiplier=[](float value) {
        return std::isfinite(value) ? std::clamp(value,0.25f,3.0f) : 1.0f;
    };
    jsonxx::Object scale,minimapScale,worldMapScale;
    minimapScale.import("icons",jsonxx::Value(multiplier(settings.miniIconSize)));
    minimapScale.import("trails",jsonxx::Value(multiplier(settings.miniTrailSize)));
    worldMapScale.import("icons",jsonxx::Value(multiplier(settings.bigIconSize)));
    worldMapScale.import("trails",jsonxx::Value(multiplier(settings.bigTrailSize)));
    scale.import("minimap",jsonxx::Value(minimapScale));
    scale.import("worldMap",jsonxx::Value(worldMapScale));
    root.import("scale",jsonxx::Value(scale));
    std::vector<const TacoPack::Pack*> ordered;
    if(ready) for(const auto& pack:packs) ordered.push_back(&pack);
    std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->path < b->path;});
    jsonxx::Array entries;
    for(const auto* pack:ordered) {
        jsonxx::Object entry;
        const auto file=Utf8(pack->path.filename());
        std::string name=file;
        for(const auto& record:catalog) if(record.filename==file && !record.name.empty()) {name=record.name;break;}
        entry.import("file",jsonxx::Value(file));
        entry.import("path",jsonxx::Value(Absolute(pack->path)));
        entry.import("name",jsonxx::Value(name));
        entry.import("loaded",jsonxx::Value(pack->loaded));
        std::vector<std::string> off;
        for(const auto& [key,category]:pack->categories) if(!category->visible) off.push_back(key);
        std::sort(off.begin(),off.end());
        jsonxx::Array disabled;
        for(const auto& key:off) disabled.import(jsonxx::Value(key));
        entry.import("off",jsonxx::Value(disabled));
        entries.import(jsonxx::Value(entry));
    }
    root.import("packs",jsonxx::Value(entries));
    const auto body=root.json();
    RaiseEvent send=nullptr;std::string payload;
    {
        std::lock_guard lock(mutex);
        if(!raiseEvent || body==lastBody) return;
        lastBody=body;active=true;++revision;lastJson=Encode(lastBody);
        snapshotReady=ready;publishedPacks=ordered.size();
        readablePacks=static_cast<size_t>(std::count_if(ordered.begin(),ordered.end(),[](const auto* pack){return pack->loaded;}));
        send=raiseEvent;payload=lastJson;++raisedEvents;
    }
    // No lock across Nexus dispatch: a consumer may request state reentrantly.
    if(send) send(EV_TAQUITO_PATHING_STATE,const_cast<char*>(payload.c_str()));
}
void PathingState::Reply() {
    SendSnapshot(true);
}
void PathingState::Rebroadcast() { SendSnapshot(false); }
PathingState::Diagnostics PathingState::GetDiagnostics() {
    std::lock_guard lock(mutex);
    return {raiseEvent!=nullptr,active,snapshotReady,revision,raisedEvents,receivedRequests,publishedPacks,readablePacks};
}
std::string PathingState::GetSnapshot() {
    std::lock_guard lock(mutex);
    return lastJson;
}
void PathingState::Shutdown() {
    RaiseEvent send=nullptr;std::string payload;
    {
        std::lock_guard lock(mutex);
        if(!raiseEvent) return;
        jsonxx::Object root;
        if(root.parse(lastBody)) {
            root.import("active",jsonxx::Value(false));
            const auto body=root.json();
            if(body!=lastBody) {lastBody=body;++revision;lastJson=Encode(lastBody);}
        }
        send=raiseEvent;payload=lastJson;raiseEvent=nullptr;active=false;
        if(!payload.empty()) ++raisedEvents;
    }
    if(!payload.empty()) send(EV_TAQUITO_PATHING_STATE,const_cast<char*>(payload.c_str()));
}
