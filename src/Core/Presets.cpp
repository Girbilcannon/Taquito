#include "Presets.h"
#include "InstalledPacks.h"
#include "AtomicFile.h"
#include "AppSettings.h"
#include "../third_party/jsonxx.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <cmath>
#include <stdexcept>
namespace {
using namespace Presets;
std::vector<Profile> profiles;Selections normal;
std::string selected,status;std::filesystem::path folder;
bool splitStore=false,indexDirty=false,normalDirty=false;std::set<std::string> dirtyProfiles;
bool enabled=false,writable=true,restoreBaseline=false;uint32_t nextId=1;
Profile* Find(const std::string&id){for(auto&p:profiles)if(p.id==id)return &p;return nullptr;}
std::string Filename(const TacoPack::Pack&p){auto s=p.path.filename().u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());}
bool CaptureInto(Selections&target,const std::vector<TacoPack::Pack>&packs){
    bool changed=false;
    for(const auto&pack:packs){const auto file=Filename(pack);for(const auto&[key,category]:pack.categories){
        auto&entries=target[file];auto it=entries.find(key);
        if(it==entries.end()||it->second!=category->visible){entries[key]=category->visible;changed=true;}
    }}return changed;
}
void ApplyFrom(const Selections&source,bool newPacksOff,std::vector<TacoPack::Pack>&packs){
    const auto installed=InstalledPacks::Load(folder/"installed-packs.json");
    for(auto&pack:packs){
        auto found=source.find(Filename(pack));
        if(found==source.end())for(const auto&record:installed)if(record.filename==Filename(pack))
            for(const auto&alias:record.aliases){auto candidate=source.find(alias);if(candidate!=source.end()){found=candidate;break;}}
        for(auto&[key,category]:pack.categories){
            if(found!=source.end()){auto choice=found->second.find(key);if(choice!=found->second.end()){category->visible=choice->second;continue;}}
            if(newPacksOff)category->visible=category->parent&&category->parent!=&pack.root;
        }
    }
}
jsonxx::Object Encode(const Selections&choices){jsonxx::Object files;for(const auto&[file,entries]:choices){jsonxx::Object categories;for(const auto&[key,value]:entries)categories.import(key,jsonxx::Value(value));files.import(file,jsonxx::Value(categories));}return files;}
bool Decode(const jsonxx::Object&files,Selections&out){
    if(files.kv_map().size()>10000)return false;
    for(const auto&[file,value]:files.kv_map()){
        if(!value->is<jsonxx::Object>())return false;
        const auto&entries=value->get<jsonxx::Object>();if(entries.kv_map().size()>200000)return false;
        for(const auto&[key,choice]:entries.kv_map()){if(!choice->is<jsonxx::Boolean>())return false;out[file][key]=choice->get<jsonxx::Boolean>();}
    }return true;
}
bool ValidId(const std::string&id){return id.starts_with("preset-")&&id.size()>7&&id.size()<64&&std::all_of(id.begin()+7,id.end(),[](char c){return c>='0'&&c<='9';});}
std::filesystem::path Store(){return folder/"Presets";}
bool ReadObject(const std::filesystem::path&path,jsonxx::Object&root){
    std::error_code ec;if(std::filesystem::file_size(path,ec)>32*1024*1024||ec)return false;
    std::ifstream input(path,std::ios::binary);std::ostringstream text;text<<input.rdbuf();return input&&root.parse(text.str());
}
bool ReadProfile(const jsonxx::Object&item,Profile&p){
    if(!item.has<jsonxx::String>("id")||!item.has<jsonxx::String>("name")||!item.has<jsonxx::Boolean>("newPacksOff")||!item.has<jsonxx::Object>("selections"))return false;
    p.id=item.get<jsonxx::String>("id");p.name=item.get<jsonxx::String>("name");p.newPacksOff=item.get<jsonxx::Boolean>("newPacksOff");
    return ValidId(p.id)&&!p.name.empty()&&p.name.size()<=255&&Decode(item.get<jsonxx::Object>("selections"),p.selections);
}
std::string UniqueName(const std::string&prefix){for(unsigned i=1;;++i){auto name=prefix+std::to_string(i);if(std::none_of(profiles.begin(),profiles.end(),[&](const auto&p){return p.name==name;}))return name;}}
std::string NewId(){std::string id;do{id="preset-"+std::to_string(nextId++);}while(Find(id));return id;}
}
void Presets::Clear(){profiles.clear();normal.clear();selected.clear();status.clear();folder.clear();splitStore=false;indexDirty=normalDirty=false;dirtyProfiles.clear();enabled=false;writable=true;restoreBaseline=false;nextId=1;}
void Presets::Initialize(const std::filesystem::path&directory,bool resume){
    AppSettings::Flush();
    Clear();folder=directory;std::error_code ec;
    auto fail=[](){profiles.clear();normal.clear();selected.clear();writable=false;status="Could not read preset data. Presets are unavailable; existing files were preserved.";};
    const auto indexPath=Store()/"presets-index.json";
    jsonxx::Object root;std::set<std::string> ids;
    const bool hasIndex=std::filesystem::exists(indexPath,ec);if(ec){fail();return;}
    if(hasIndex){
        if(!ReadObject(indexPath,root)||!root.has<jsonxx::Number>("schema")||root.get<jsonxx::Number>("schema")!=2||!root.has<jsonxx::String>("selected")||!root.has<jsonxx::Array>("presets")){fail();return;}
        jsonxx::Object baseline;if(!ReadObject(Store()/"normal.json",baseline)||!baseline.has<jsonxx::Number>("schema")||baseline.get<jsonxx::Number>("schema")!=1||!baseline.has<jsonxx::Object>("normal")||!Decode(baseline.get<jsonxx::Object>("normal"),normal)){fail();return;}
        const auto&items=root.get<jsonxx::Array>("presets");if(items.size()>128){fail();return;}
        for(unsigned i=0;i<items.size();++i){
            if(!items.has<jsonxx::String>(i)){fail();return;}const auto id=items.get<jsonxx::String>(i);
            jsonxx::Object item;Profile p;
            if(!ValidId(id)||!ids.insert(id).second||!ReadObject(Store()/(id+".json"),item)||!item.has<jsonxx::Number>("schema")||item.get<jsonxx::Number>("schema")!=1||!ReadProfile(item,p)||p.id!=id){fail();return;}profiles.push_back(std::move(p));
        }
        splitStore=true;
    }else{
        const auto legacy=folder/"presets.json";const bool hasLegacy=std::filesystem::exists(legacy,ec);if(ec){fail();return;}if(!hasLegacy)return;
        if(!ReadObject(legacy,root)||!root.has<jsonxx::Number>("schema")||root.get<jsonxx::Number>("schema")!=1||!root.has<jsonxx::Array>("presets")||!root.has<jsonxx::String>("selected")||!root.has<jsonxx::Object>("normal")||!Decode(root.get<jsonxx::Object>("normal"),normal)){fail();return;}
        const auto&items=root.get<jsonxx::Array>("presets");if(items.size()>128){fail();return;}
        for(unsigned i=0;i<items.size();++i){Profile p;if(!items.has<jsonxx::Object>(i)||!ReadProfile(items.get<jsonxx::Object>(i),p)||!ids.insert(p.id).second){fail();return;}profiles.push_back(std::move(p));}
    }
    selected=root.get<jsonxx::String>("selected");if(!profiles.empty()&&!Find(selected)){fail();return;}
    restoreBaseline=resume&&!normal.empty();
}

bool Presets::SetEnabled(bool value,std::vector<TacoPack::Pack>&packs){
    if(value&&!writable)return false;
    if(enabled==value)return true;
    if(value){if(!restoreBaseline)normalDirty|=CaptureInto(normal,packs);restoreBaseline=false;
        if(profiles.empty()){Profile first;first.id=NewId();first.name="Preset 1";CaptureInto(first.selections,packs);selected=first.id;dirtyProfiles.insert(first.id);indexDirty=true;profiles.push_back(std::move(first));}
        enabled=true;Apply(packs);
    }else{Capture(packs);enabled=false;ApplyFrom(normal,false,packs);}
    return true;
}
bool Presets::Enabled(){return enabled;}
void Presets::Capture(const std::vector<TacoPack::Pack>&packs){if(enabled)if(auto*p=Find(selected))if(CaptureInto(p->selections,packs))dirtyProfiles.insert(p->id);}
void Presets::Apply(std::vector<TacoPack::Pack>&packs){if(enabled)if(auto*p=Find(selected)){
    // Keep defaults for newly installed packs/categories in the non-preset context too.
    for(const auto&pack:packs){const auto file=Filename(pack);for(const auto&[key,category]:pack.categories)normalDirty|=normal[file].try_emplace(key,category->visible).second;}
    ApplyFrom(p->selections,p->newPacksOff,packs);
}}
bool Presets::Save(){
    if(!writable||folder.empty())return false;if(profiles.empty())return true;
    const auto store=Store(),indexPath=store/"presets-index.json",normalPath=store/"normal.json";
    bool changed=indexDirty||!splitStore||AppSettings::SaveFailed(indexPath);
    if(normalDirty||!splitStore||AppSettings::SaveFailed(normalPath)){
        AppSettings::QueueSerializedSave(normalPath,[normal=normal]{jsonxx::Object root;root.import("schema",jsonxx::Value(1));root.import("normal",jsonxx::Value(Encode(normal)));return root.json();},32*1024*1024);changed=true;
    }
    std::vector<std::string> ids;
    for(const auto&p:profiles){
        ids.push_back(p.id);const auto path=store/(p.id+".json");
        if(!splitStore||dirtyProfiles.count(p.id)||AppSettings::SaveFailed(path)){
            AppSettings::QueueSerializedSave(path,[p=p]{jsonxx::Object item;item.import("schema",jsonxx::Value(1));item.import("id",jsonxx::Value(p.id));item.import("name",jsonxx::Value(p.name));item.import("newPacksOff",jsonxx::Value(p.newPacksOff));item.import("selections",jsonxx::Value(Encode(p.selections)));return item.json();},32*1024*1024);changed=true;
        }
    }
    if(changed){
        // The single writer sorts paths: normal.json and preset-N.json finish before presets-index.json.
        // Publish the index only after every referenced file is present and its write succeeded.
        AppSettings::QueueSerializedSave(indexPath,[store,ids=std::move(ids),selected=selected]{
            std::error_code ec;
            if(AppSettings::SaveFailed(store/"normal.json")||!std::filesystem::exists(store/"normal.json",ec))throw std::runtime_error("Preset baseline not saved");
            jsonxx::Array items;for(const auto&id:ids){const auto path=store/(id+".json");if(AppSettings::SaveFailed(path)||!std::filesystem::exists(path,ec))throw std::runtime_error("Preset not saved");items.import(jsonxx::Value(id));}
            jsonxx::Object root;root.import("schema",jsonxx::Value(2));root.import("selected",jsonxx::Value(selected));root.import("presets",jsonxx::Value(items));return root.json();
        });
    }
    splitStore=true;indexDirty=normalDirty=false;dirtyProfiles.clear();status.clear();return true;
}
const std::vector<Presets::Profile>&Presets::Items(){return profiles;}
const std::string&Presets::Selected(){return selected;}
const std::string&Presets::Status(){
    if(writable){const std::string message="Could not save preset data. Current selections remain in memory.";
        if(AppSettings::SaveFailed(Store()/"presets-index.json"))status=message;else if(status==message)status.clear();}
    return status;
}
bool Presets::Select(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||!Find(id)||id==selected)return false;Capture(packs);selected=id;indexDirty=true;Apply(packs);return true;}
bool Presets::Step(int direction,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()<2||!direction)return false;size_t index=0;while(index<profiles.size()&&profiles[index].id!=selected)++index;const int n=static_cast<int>(profiles.size());return Select(profiles[(static_cast<int>(index)+direction%n+n)%n].id,packs);}
bool Presets::Add(std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()>=128)return false;Capture(packs);Profile p;p.id=NewId();p.name=UniqueName("Preset ");p.newPacksOff=true;selected=p.id;dirtyProfiles.insert(p.id);indexDirty=true;profiles.push_back(std::move(p));Apply(packs);Capture(packs);return true;}
bool Presets::Duplicate(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()>=128)return false;Capture(packs);auto*source=Find(id);if(!source)return false;Profile p=*source;p.id=NewId();const auto prefix=source->name.substr(0,220)+" (Duplicate ";for(unsigned n=1;;++n){p.name=prefix+std::to_string(n)+")";if(std::none_of(profiles.begin(),profiles.end(),[&](const auto&i){return i.name==p.name;}))break;}selected=p.id;dirtyProfiles.insert(p.id);indexDirty=true;profiles.push_back(std::move(p));Apply(packs);return true;}
bool Presets::Rename(const std::string&id,const std::string&name){auto*p=Find(id);auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");if(!enabled||!p||first==std::string::npos||name.size()>255)return false;p->name=name.substr(first,last-first+1);dirtyProfiles.insert(id);return true;}
bool Presets::Move(const std::string&id,const std::string&target,bool after){
    if(!enabled||id==target)return false;
    auto source=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==id;});
    auto destination=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==target;});
    if(source==profiles.end()||destination==profiles.end())return false;
    const auto from=source-profiles.begin();auto to=destination-profiles.begin()+(after?1:0);
    if(from<to)--to;
    if(from==to)return false;
    auto moved=std::move(*source);profiles.erase(source);profiles.insert(profiles.begin()+to,std::move(moved));indexDirty=true;return true;
}
bool Presets::Remove(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()<2)return false;auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==id;});if(it==profiles.end())return false;Capture(packs);size_t index=it-profiles.begin();bool active=selected==id;profiles.erase(it);indexDirty=true;dirtyProfiles.erase(id);if(active){selected=profiles[std::min(index,profiles.size()-1)].id;Apply(packs);}return true;}
