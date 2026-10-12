#include "InstalledPacks.h"
#include "OnlinePacks.h"
#include "../third_party/jsonxx.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
std::string Field(const jsonxx::Object& row,const char* name) {
    return row.has<jsonxx::String>(name) ? row.get<jsonxx::String>(name) : std::string{};
}
bool SafeFilename(const std::string& name) {
    if(name.empty() || name.size()>150 || name=="." || name=="..") return false;
    for(unsigned char c:name) if(!(std::isalnum(c)||c=='_'||c=='-'||c=='.')) return false;
    return name.ends_with(".taco")||name.ends_with(".zip");
}
std::string Lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}
int VersionCompare(const std::string& old, const std::string& current, bool& numeric) {
    auto parse=[](std::string text,std::vector<unsigned long long>& parts) {
        if(!text.empty() && (text[0]=='v'||text[0]=='V')) text.erase(0,1);
        if(text.empty()) return false;
        std::stringstream stream(text);
        std::string part;
        while(std::getline(stream,part,'.')) {
            if(part.empty() || part.size()>18 || !std::all_of(part.begin(),part.end(),[](unsigned char c){return std::isdigit(c)!=0;})) return false;
            try { parts.push_back(std::stoull(part)); } catch(...) { return false; }
        }
        return !parts.empty();
    };
    std::vector<unsigned long long> a,b;
    numeric=parse(old,a)&&parse(current,b);
    if(!numeric) return 0;
    const auto count=std::max(a.size(),b.size());
    for(size_t i=0;i<count;++i) {
        const auto x=i<a.size()?a[i]:0, y=i<b.size()?b[i]:0;
        if(x!=y) return x<y ? 1:-1;
    }
    return 0;
}
}
std::vector<InstalledPacks::Record> InstalledPacks::Load(const std::filesystem::path& manifest) {
    std::ifstream input(manifest,std::ios::binary);
    jsonxx::Object root;
    if(!input || !root.parse(input) || !root.has<jsonxx::Array>("packs")) return {};
    std::vector<Record> records;
    const auto& array=root.get<jsonxx::Array>("packs");
    for(unsigned i=0;i<array.size() && i<10000;++i) {
        if(!array.has<jsonxx::Object>(i)) continue;
        const auto& item=array.get<jsonxx::Object>(i);
        Record r{Field(item,"name"),Field(item,"author"),Field(item,"repository"),
                 Field(item,"filename"),Field(item,"version"),Field(item,"lastUpdate"),Field(item,"downloadUrl")};
        if(item.has<jsonxx::Array>("aliases")) {
            const auto& aliases=item.get<jsonxx::Array>("aliases");
            for(unsigned j=0;j<aliases.size() && j<100;++j)
                if(aliases.has<jsonxx::String>(j) && SafeFilename(aliases.get<jsonxx::String>(j)))
                    r.aliases.push_back(aliases.get<jsonxx::String>(j));
        }
        if(SafeFilename(r.filename)) records.push_back(std::move(r));
    }
    return records;
}
bool InstalledPacks::IsValid(const std::filesystem::path& manifest) {
    std::error_code ec;
    if(!std::filesystem::exists(manifest,ec)) return !ec;
    std::ifstream input(manifest,std::ios::binary);
    jsonxx::Object root;
    return input && root.parse(input) && root.has<jsonxx::Array>("packs");
}
bool InstalledPacks::Save(const std::filesystem::path& manifest,const std::vector<Record>& records) {
    jsonxx::Array array;
    for(const auto& r:records) {
        jsonxx::Object row;
        row.import("name",jsonxx::Value(r.name)); row.import("author",jsonxx::Value(r.author));
        row.import("repository",jsonxx::Value(r.repository)); row.import("filename",jsonxx::Value(r.filename));
        row.import("version",jsonxx::Value(r.version)); row.import("lastUpdate",jsonxx::Value(r.lastUpdate));
        row.import("downloadUrl",jsonxx::Value(r.downloadUrl));
        jsonxx::Array aliases;
        for(const auto& alias:r.aliases) aliases.append(jsonxx::Value(alias));
        row.import("aliases",jsonxx::Value(aliases));
        array.append(row);
    }
    jsonxx::Object root;
    root.import("packs",jsonxx::Value(array));
    const auto temporary=manifest.string()+".new";
    { std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
      if(!output) return false;
      output << root.json(); output.close();
      if(!output) return false;
    }
#ifdef _WIN32
    if(MoveFileExW(std::filesystem::path(temporary).c_str(),manifest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
#else
    { std::error_code renameError;
      std::filesystem::rename(temporary,manifest,renameError);
      if(!renameError) return true;
    }
#endif
    std::error_code ec;
    std::filesystem::remove(temporary,ec);
    return false;
}
InstalledPacks::Record InstalledPacks::FromCatalog(const OnlinePacks::Entry& e) {
    return {e.name,e.authorUsername,e.repository,e.filename,e.version,e.lastUpdate,e.downloadUrl};
}
const OnlinePacks::Entry* InstalledPacks::Match(const Record& r,const std::vector<OnlinePacks::Entry>& entries) {
    auto unique=[&](auto predicate) {
        const OnlinePacks::Entry* found=nullptr;
        for(const auto& e:entries) if(predicate(e)) { if(found) return static_cast<const OnlinePacks::Entry*>(nullptr); found=&e; }
        return found;
    };
    // Pack filenames normally stay stable across release titles and version tags.
    if(auto* e=unique([&](const auto& x){return Lower(x.filename)==Lower(r.filename);})) return e;
    const auto sameName=[&](const OnlinePacks::Entry& e){return Lower(e.name)==Lower(r.name);};
    if(!r.repository.empty()) {
        if(auto* e=unique([&](const auto& x){return x.repository==r.repository && sameName(x);} )) return e;
    }
    if(!r.author.empty() && !r.name.empty()) {
        if(auto* e=unique([&](const auto& x){return Lower(x.authorUsername)==Lower(r.author) && sameName(x);} )) return e;
    }
    // A changed display name is safe only when this repository identifies one pack.
    if(!r.repository.empty()) return unique([&](const auto& x){return x.repository==r.repository;});
    return nullptr;
}
bool InstalledPacks::HasNewerRelease(const Record& installed,const OnlinePacks::Entry& available) {
    bool numeric=false;
    const int comparison=VersionCompare(installed.version,available.version,numeric);
    if(comparison<0) return false;
    if(comparison>0) return true;
    if(!installed.lastUpdate.empty() && !available.lastUpdate.empty()) {
        if(available.lastUpdate<installed.lastUpdate) return false;
        if(available.lastUpdate>installed.lastUpdate) return true;
    }
    if(!numeric && !available.version.empty() && available.version!=installed.version) return true;
    return available.filename!=installed.filename || available.downloadUrl!=installed.downloadUrl;
}
