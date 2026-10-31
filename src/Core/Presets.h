#pragma once
#include "MarkerPack.h"
#include <map>
namespace Presets {
using Selections=std::map<std::string,std::map<std::string,bool>>;
struct Profile { std::string id,name;bool newPacksOff=false;Selections selections; };
void Initialize(const std::filesystem::path& directory,bool resume=false);
void Clear();
bool SetEnabled(bool enabled,std::vector<TacoPack::Pack>& packs);
bool Enabled();
void Capture(const std::vector<TacoPack::Pack>& packs);
void Apply(std::vector<TacoPack::Pack>& packs);
bool Save();
const std::vector<Profile>& Items();
const std::string& Selected();
const std::string& Status();
bool Select(const std::string& id,std::vector<TacoPack::Pack>& packs);
bool Step(int direction,std::vector<TacoPack::Pack>& packs);
bool Add(std::vector<TacoPack::Pack>& packs);
bool Duplicate(const std::string& id,std::vector<TacoPack::Pack>& packs);
bool Rename(const std::string& id,const std::string& name);
bool Move(const std::string& id,const std::string& target,bool after);
bool Remove(const std::string& id,std::vector<TacoPack::Pack>& packs);
}
