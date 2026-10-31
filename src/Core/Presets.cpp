#include "Presets.h"
#include "InstalledPacks.h"
#include "AtomicFile.h"
#include "../third_party/jsonxx.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <cmath>
namespace {
using namespace Presets;
std::vector<Profile> profiles;Selections normal;
std::string selected,status,lastSaved;std::filesystem::path folder;
bool enabled=false,writable=true,restoreBaseline=false;uint32_t nextId=1;
Profile* Find(const std::string&id){for(auto&p:profiles)if(p.id==id)return &p;return nullptr;}
std::string Filename(const TacoPack::Pack&p){auto s=p.path.filename().u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());}
void CaptureInto(Selections&target,const std::vector<TacoPack::Pack>&packs){
    for(const auto&pack:packs)for(const auto&[key,category]:pack.categories)target[Filename(pack)][key]=category->visible;
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
std::string UniqueName(const std::string&prefix){for(unsigned i=1;;++i){auto name=prefix+std::to_string(i);if(std::none_of(profiles.begin(),profiles.end(),[&](const auto&p){return p.name==name;}))return name;}}
std::string NewId(){std::string id;do{id="preset-"+std::to_string(nextId++);}while(Find(id));return id;}
}
void Presets::Clear(){profiles.clear();normal.clear();selected.clear();status.clear();lastSaved.clear();folder.clear();enabled=false;writable=true;restoreBaseline=false;nextId=1;}
void Presets::Initialize(const std::filesystem::path&directory,bool resume){
    Clear();folder=directory;const auto path=folder/"presets.json";std::error_code ec;
    if(!std::filesystem::exists(path,ec))return;
    auto fail=[](){profiles.clear();normal.clear();selected.clear();writable=false;status="Could not read presets.json. Presets are unavailable; the existing file was preserved.";};
    if(std::filesystem::file_size(path,ec)>32*1024*1024||ec){fail();return;}
    std::ifstream input(path,std::ios::binary);std::ostringstream text;text<<input.rdbuf();jsonxx::Object root;
    if(!input||!root.parse(text.str())||!root.has<jsonxx::Number>("schema")||root.get<jsonxx::Number>("schema")!=1||!root.has<jsonxx::Array>("presets")||!root.has<jsonxx::String>("selected")||!root.has<jsonxx::Object>("normal")){fail();return;}
    const auto&items=root.get<jsonxx::Array>("presets");if(items.size()>128){fail();return;}
    std::set<std::string> ids;
    for(unsigned i=0;i<items.size();++i){if(!items.has<jsonxx::Object>(i)){fail();return;}const auto&item=items.get<jsonxx::Object>(i);Profile p;
        if(!item.has<jsonxx::String>("id")||!item.has<jsonxx::String>("name")||!item.has<jsonxx::Boolean>("newPacksOff")||!item.has<jsonxx::Object>("selections")){fail();return;}
        p.id=item.get<jsonxx::String>("id");p.name=item.get<jsonxx::String>("name");p.newPacksOff=item.get<jsonxx::Boolean>("newPacksOff");
        if(p.id.empty()||p.name.empty()||p.name.size()>255||!ids.insert(p.id).second||!Decode(item.get<jsonxx::Object>("selections"),p.selections)){fail();return;}profiles.push_back(std::move(p));
    }
    selected=root.get<jsonxx::String>("selected");if((!profiles.empty()&&!Find(selected))||!Decode(root.get<jsonxx::Object>("normal"),normal)){fail();return;}
    restoreBaseline=resume&&!normal.empty();
}
bool Presets::SetEnabled(bool value,std::vector<TacoPack::Pack>&packs){
    if(value&&!writable)return false;
    if(enabled==value)return true;
    if(value){if(!restoreBaseline)CaptureInto(normal,packs);restoreBaseline=false;
        if(profiles.empty()){Profile first;first.id=NewId();first.name="Preset 1";CaptureInto(first.selections,packs);selected=first.id;profiles.push_back(std::move(first));}
        enabled=true;Apply(packs);
    }else{Capture(packs);enabled=false;ApplyFrom(normal,false,packs);}
    return true;
}
bool Presets::Enabled(){return enabled;}
void Presets::Capture(const std::vector<TacoPack::Pack>&packs){if(enabled)if(auto*p=Find(selected))CaptureInto(p->selections,packs);}
void Presets::Apply(std::vector<TacoPack::Pack>&packs){if(enabled)if(auto*p=Find(selected)){
    // Keep defaults for newly installed packs/categories in the non-preset context too.
    for(const auto&pack:packs)for(const auto&[key,category]:pack.categories)normal[Filename(pack)].try_emplace(key,category->visible);
    ApplyFrom(p->selections,p->newPacksOff,packs);
}}
bool Presets::Save(){
    if(!writable||folder.empty())return false;if(profiles.empty())return true;
    jsonxx::Object root;root.import("schema",jsonxx::Value(1));root.import("selected",jsonxx::Value(selected));root.import("normal",jsonxx::Value(Encode(normal)));jsonxx::Array items;
    for(const auto&p:profiles){jsonxx::Object item;item.import("id",jsonxx::Value(p.id));item.import("name",jsonxx::Value(p.name));item.import("newPacksOff",jsonxx::Value(p.newPacksOff));item.import("selections",jsonxx::Value(Encode(p.selections)));items.import(jsonxx::Value(item));}
    root.import("presets",jsonxx::Value(items));auto body=root.json();if(body.size()>32*1024*1024){status="Preset data is too large to save; the previous file was preserved.";return false;}if(body==lastSaved)return true;
    if(!AtomicFile::Write(folder/"presets.json",body)){status="Could not save presets.json. Current selections remain in memory.";return false;}
    lastSaved=body;status.clear();return true;
}
const std::vector<Presets::Profile>&Presets::Items(){return profiles;}
const std::string&Presets::Selected(){return selected;}
const std::string&Presets::Status(){return status;}
bool Presets::Select(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||!Find(id)||id==selected)return false;Capture(packs);selected=id;Apply(packs);return true;}
bool Presets::Step(int direction,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()<2||!direction)return false;size_t index=0;while(index<profiles.size()&&profiles[index].id!=selected)++index;const int n=static_cast<int>(profiles.size());return Select(profiles[(static_cast<int>(index)+direction%n+n)%n].id,packs);}
bool Presets::Add(std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()>=128)return false;Capture(packs);Profile p;p.id=NewId();p.name=UniqueName("Preset ");p.newPacksOff=true;selected=p.id;profiles.push_back(std::move(p));Apply(packs);Capture(packs);return true;}
bool Presets::Duplicate(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()>=128)return false;Capture(packs);auto*source=Find(id);if(!source)return false;Profile p=*source;p.id=NewId();const auto prefix=source->name.substr(0,220)+" (Duplicate ";for(unsigned n=1;;++n){p.name=prefix+std::to_string(n)+")";if(std::none_of(profiles.begin(),profiles.end(),[&](const auto&i){return i.name==p.name;}))break;}selected=p.id;profiles.push_back(std::move(p));Apply(packs);return true;}
bool Presets::Rename(const std::string&id,const std::string&name){auto*p=Find(id);auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");if(!enabled||!p||first==std::string::npos||name.size()>255)return false;p->name=name.substr(first,last-first+1);return true;}
bool Presets::Move(const std::string&id,const std::string&target,bool after){
    if(!enabled||id==target)return false;
    auto source=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==id;});
    auto destination=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==target;});
    if(source==profiles.end()||destination==profiles.end())return false;
    const auto from=source-profiles.begin();auto to=destination-profiles.begin()+(after?1:0);
    if(from<to)--to;
    if(from==to)return false;
    auto moved=std::move(*source);profiles.erase(source);profiles.insert(profiles.begin()+to,std::move(moved));return true;
}
bool Presets::Remove(const std::string&id,std::vector<TacoPack::Pack>&packs){if(!enabled||profiles.size()<2)return false;auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto&p){return p.id==id;});if(it==profiles.end())return false;Capture(packs);size_t index=it-profiles.begin();bool active=selected==id;profiles.erase(it);if(active){selected=profiles[std::min(index,profiles.size()-1)].id;Apply(packs);}return true;}
