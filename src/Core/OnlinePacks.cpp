#include "OnlinePacks.h"
#include "InstalledPacks.h"
#include "AppRuntime.h"
#include "../third_party/jsonxx.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_HEADER_FILE_ONLY
#include "../third_party/miniz.c"
#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace OnlinePacks {
namespace {
constexpr wchar_t CatalogUrl[] = L"https://mp-repo.blishhud.com/repo.json";
constexpr size_t MaxCatalog = 4 * 1024 * 1024, MaxArchive = 128 * 1024 * 1024;
std::filesystem::path directory;
std::vector<Entry> entries;
std::string status;
std::string updateStatus;
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
std::atomic_bool reloadRequested{false};
bool stopping = false, loading = false, updateQueued = false;
enum class Kind { Catalog, Description, Archive, CheckUpdates };
struct Job { Kind kind; std::string id; };
std::vector<Job> jobs;

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if (n<=0) return {};
    std::wstring result(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n);
    return result;
}
std::string Lower(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return text;
}
std::string Field(const jsonxx::Object& object, const char* name) {
    // Blish HUD's JSON deserializer accepts camelCase and PascalCase names.
    const auto wanted=Lower(name);
    for(const auto& kv:object.kv_map())
        if(Lower(kv.first)==wanted && object.has<jsonxx::String>(kv.first)) return object.get<jsonxx::String>(kv.first);
    return {};
}
bool SafeFilename(const std::string& filename) {
    if (filename.empty() || filename.size()>150 || filename=="." || filename=="..") return false;
    for (unsigned char c:filename) if (!(std::isalnum(c) || c=='_' || c=='-' || c=='.')) return false;
    return filename.ends_with(".taco") || filename.ends_with(".zip");
}
std::string Repository(const std::string& url) {
    constexpr const char* prefix="https://github.com/";
    if (!url.starts_with(prefix)) return {};
    const auto rest=url.substr(std::char_traits<char>::length(prefix));
    const auto slash=rest.find('/');
    if (slash==std::string::npos) return {};
    const auto end=rest.find_first_of("/?#",slash+1);
    const auto owner=rest.substr(0,slash), repo=rest.substr(slash+1,end==std::string::npos ? end : end-slash-1);
    auto valid=[](const std::string& value) { if(value.empty() || value.size()>100) return false;
        for(unsigned char c:value) if(!(std::isalnum(c)||c=='_'||c=='-'||c=='.')) return false; return true; };
    return valid(owner)&&valid(repo) ? std::string(prefix)+owner+"/"+repo : std::string{};
}
Entry* Find(const std::string& id) {
    for (auto& item:entries) if (item.id==id) return &item;
    return nullptr;
}
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET value=nullptr):h(value){}
    ~Handle(){ if(h) WinHttpCloseHandle(h); }
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
bool Fetch(const std::wstring& url, size_t maxBytes, std::string* body,
           const std::filesystem::path* target, std::string& error) {
    if (!url.starts_with(L"https://")) { error="Only HTTPS downloads are accepted."; return false; }
    URL_COMPONENTS parts{}; parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(),0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS) {
        error="Invalid download URL."; return false;
    }
    const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    Handle session(WinHttpOpen(L"Mozilla/5.0 (compatible; Taquito/0.0.1.12)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.h) { error="Could not start an HTTPS session."; return false; }
    WinHttpSetTimeouts(session.h,5000,5000,12000,20000);
    Handle connection(WinHttpConnect(session.h,host.c_str(),parts.nPort,0));
    Handle request(connection.h ? WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE) : nullptr);
    if(!request.h) { error="Could not open the download request."; return false; }
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    if(!WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       !WinHttpReceiveResponse(request.h,nullptr)) { error="The server did not respond."; return false; }
    DWORD code=0,size=sizeof(code);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX) || code!=200) {
        error="HTTP " + std::to_string(code); return false;
    }
    std::ofstream output;
    if(target) { output.open(*target,std::ios::binary|std::ios::trunc); if(!output) { error="Could not create a download file."; return false; } }
    char buffer[64*1024]; size_t total=0;
    for(;;) {
        DWORD received=0;
        if(!WinHttpReadData(request.h,buffer,sizeof(buffer),&received)) { error="Download interrupted."; return false; }
        if(!received) break;
        total+=received;
        if(total>maxBytes) { error="Download exceeds the allowed size."; return false; }
        if(target) { output.write(buffer,received); if(!output) { error="Could not write the download."; return false; } }
        else body->append(buffer,received);
    }
    if(target) { output.close(); if(!output) { error="Could not finish the download."; return false; } }
    if(!total) { error="The server returned an empty file."; return false; }
    return true;
}
bool ValidArchive(const std::filesystem::path& path) {
    mz_zip_archive zip{};
    if(!mz_zip_reader_init_file(&zip,path.string().c_str(),0)) return false;
    bool xml=false;
    for(mz_uint i=0;i<mz_zip_reader_get_num_files(&zip);++i) {
        mz_zip_archive_file_stat stat{};
        if(mz_zip_reader_file_stat(&zip,i,&stat)) {
            std::string name=stat.m_filename;
            std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(name.ends_with(".xml")) { xml=true; break; }
        }
    }
    mz_zip_reader_end(&zip);
    return xml;
}
// Refresh this list only on demand; its Version and LastUpdate fields drive manual checks.
void FetchCatalog(bool keepLoading=false) {
    std::string body,error;
    if(!Fetch(CatalogUrl,MaxCatalog,&body,nullptr,error)) {
        std::lock_guard lock(mutex); status="Catalog: "+error; loading=false; return;
    }
    jsonxx::Array list;
    if(!list.parse(body)) {
        std::lock_guard lock(mutex); status="The catalog returned invalid JSON."; loading=false; return;
    }
    std::vector<Entry> found;
    for(unsigned i=0;i<list.size();++i) {
        if(!list.has<jsonxx::Object>(i)) continue;
        const auto& row=list.get<jsonxx::Object>(i);
        Entry item;
        item.name=Field(row,"Name"); item.filename=Field(row,"FileName");
        item.downloadUrl=Field(row,"Download"); item.pageUrl=Field(row,"Info");
        item.description=Field(row,"Description"); item.descriptionReady=!item.description.empty();
        item.categories=Field(row,"Categories"); item.author=Field(row,"AuthorName");
        item.authorUsername=Field(row,"AuthorUsername");
        item.version=Field(row,"Version"); item.lastUpdate=Field(row,"LastUpdate");
        item.id=item.filename;
        item.repository=Repository(item.pageUrl);
        if(item.repository.empty()) item.repository=Repository(item.downloadUrl);
        if(item.name.empty() || !SafeFilename(item.filename) ||
           !item.downloadUrl.starts_with("https://")) continue;
        if(std::any_of(found.begin(),found.end(),[&](const Entry& previous){return previous.id==item.id;})) continue;
        found.push_back(std::move(item));
    }
    const auto installed=InstalledPacks::Load(AppRuntime::GetAddonDirectory()/"installed-packs.json");
    for(auto& item:found) for(const auto& record:installed)
        if(record.filename==item.filename) { item.installedVersion=record.version; break; }
    std::lock_guard lock(mutex);
    entries=std::move(found);
    status=entries.empty() ? "The catalog contains no supported packs." :
           "Blish HUD catalog: " + std::to_string(entries.size()) + " packs";
    loading=keepLoading;
}
void FetchDescription(const std::string& id) {
    std::string repository;
    { std::lock_guard lock(mutex); if(auto* item=Find(id)) repository=item->repository; }
    std::string description="No repository description is available.";
    if(!repository.empty()) {
        const std::wstring url=L"https://api.github.com/repos/"+Wide(repository.substr(std::string("https://github.com/").size()));
        std::string body,error;
        if(Fetch(url,MaxCatalog,&body,nullptr,error)) {
            jsonxx::Object json;
            if(json.parse(body) && json.has<jsonxx::String>("description"))
                description=json.get<jsonxx::String>("description");
        } else description="Repository description unavailable ("+error+").";
    }
    std::lock_guard lock(mutex);
    if(auto* item=Find(id)) { item->description=std::move(description); item->descriptionPending=false; item->descriptionReady=true; }
}
// Install the validated archive and write its manifest record as one recoverable operation.
// Only a recorded pack can be replaced by the automatic update path.
bool Install(const Entry& selected, bool automatic, std::string& result,
             const std::string& trackedFilename = {}) {
    std::error_code ec;
    std::filesystem::create_directories(directory,ec);
    if(ec) { result="Could not create TacoPacks."; return false; }
    const auto manifest=AppRuntime::GetAddonDirectory()/"installed-packs.json";
    if(!InstalledPacks::IsValid(manifest)) { result="Pack version file is invalid; no files were changed."; return false; }
    auto records=InstalledPacks::Load(manifest);
    size_t index=records.size();
    for(size_t i=0;i<records.size();++i) {
        if((!trackedFilename.empty() && records[i].filename==trackedFilename) ||
           records[i].filename==selected.filename ||
           (!selected.repository.empty() && records[i].repository==selected.repository &&
            Lower(records[i].name)==Lower(selected.name)) ||
           (!selected.authorUsername.empty() && Lower(records[i].author)==Lower(selected.authorUsername) &&
            Lower(records[i].name)==Lower(selected.name))) { index=i; break; }
    }
    if(automatic && index==records.size()) { result="Pack is not tracked by Taquito."; return false; }
    const auto destination=directory/Wide(selected.filename);
    const auto previous=index<records.size() ? directory/Wide(records[index].filename) : destination;
    if(automatic && !std::filesystem::exists(previous,ec)) { result="Tracked pack is missing; skipped."; return false; }
    if(automatic && previous!=destination && std::filesystem::exists(destination,ec)) {
        result="New filename already exists; skipped to protect that pack."; return false;
    }
    const auto temporary=directory/Wide(selected.filename+".download");
    auto backup=previous;
    backup += L".taquito-backup";
    if(std::filesystem::exists(backup,ec)) { result="A previous backup exists; resolve it before updating."; return false; }
    std::filesystem::remove(temporary,ec);
    std::string error;
    if(!Fetch(Wide(selected.downloadUrl),MaxArchive,nullptr,&temporary,error)) result=error;
    else if(!ValidArchive(temporary)) result="Downloaded file is not a marker pack ZIP.";
    else {
        const bool hadPrevious=std::filesystem::exists(previous,ec);
        if(hadPrevious && !MoveFileExW(previous.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))
            result="Could not move the existing pack into a backup.";
        else if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)) {
            result="Could not install the new pack.";
            if(hadPrevious) MoveFileExW(backup.c_str(),previous.c_str(),MOVEFILE_WRITE_THROUGH);
        } else {
            auto replacement=InstalledPacks::FromCatalog(selected);
            if(index<records.size()) {
                replacement.aliases=records[index].aliases;
                if(records[index].filename!=replacement.filename &&
                   std::find(replacement.aliases.begin(),replacement.aliases.end(),records[index].filename)==replacement.aliases.end())
                    replacement.aliases.push_back(records[index].filename);
            }
            if(index<records.size()) records[index]=replacement;
            else records.push_back(replacement);
            if(!InstalledPacks::Save(manifest,records)) {
                result="Could not save pack versions; restoring the previous pack.";
                std::filesystem::remove(destination,ec);
                if(hadPrevious) MoveFileExW(backup.c_str(),previous.c_str(),MOVEFILE_WRITE_THROUGH);
            } else {
                if(hadPrevious) std::filesystem::remove(backup,ec);
                result=automatic ? "Updated in TacoPacks." : "Downloaded into TacoPacks.";
                if(!automatic) reloadRequested.store(true);
            }
        }
    }
    std::filesystem::remove(temporary,ec);
    return result=="Updated in TacoPacks." || result=="Downloaded into TacoPacks.";
}
void FetchArchive(const std::string& id) {
    Entry selected;
    { std::lock_guard lock(mutex); if(auto* item=Find(id)) selected=*item; else return; }
    std::string result;
    Install(selected,false,result);
    std::lock_guard lock(mutex);
    if(auto* item=Find(id)) {
        item->downloading=false; item->downloadStatus=std::move(result);
        if(item->downloadStatus=="Downloaded into TacoPacks.") item->installedVersion=item->version;
    }
}
void UpdateTracked() {
    const auto manifest=AppRuntime::GetAddonDirectory()/"installed-packs.json";
    if(!InstalledPacks::IsValid(manifest)) {
        std::lock_guard lock(mutex); updateStatus="Pack version file is invalid; no updates installed.";
        loading=false; updateQueued=false; return;
    }
    auto records=InstalledPacks::Load(manifest);
    std::vector<Entry> catalog;
    { std::lock_guard lock(mutex); catalog=entries; }
    unsigned updated=0,failed=0,unmatched=0;
    for(const auto& record:records) {
        // Never scan or rewrite manually installed .taco files.
        const Entry* latest=InstalledPacks::Match(record,catalog);
        if(!latest) { ++unmatched; continue; }
        if(!InstalledPacks::HasNewerRelease(record,*latest)) continue;
        std::string result;
        if(Install(*latest,true,result,record.filename)) ++updated;
        else ++failed;
        { std::lock_guard lock(mutex); if(auto* item=Find(latest->id)) {
            item->downloadStatus=result;
            if(result=="Updated in TacoPacks.") item->installedVersion=item->version;
        } }
    }
    if(updated) reloadRequested.store(true);
    std::lock_guard lock(mutex);
    if(records.empty()) updateStatus="No Taquito-downloaded packs to check.";
    else if(!updated && !failed && !unmatched) updateStatus="Tracked packs are up to date.";
    else updateStatus=std::to_string(updated)+" updated, "+std::to_string(failed)+" failed, "+
        std::to_string(unmatched)+" unmatched.";
    loading=false; updateQueued=false;
}
void Run() {
    for(;;) {
        Job job;
        { std::unique_lock lock(mutex); wake.wait(lock,[]{return stopping||!jobs.empty();});
          if(stopping) return; job=std::move(jobs.front()); jobs.erase(jobs.begin()); }
        if(job.kind==Kind::Catalog) FetchCatalog();
        else if(job.kind==Kind::CheckUpdates) {
            FetchCatalog(true);
            bool hasCatalog=false;
            { std::lock_guard lock(mutex); hasCatalog=!entries.empty() && status.starts_with("Blish HUD catalog:"); }
            if(hasCatalog) UpdateTracked();
            else { std::lock_guard lock(mutex); updateStatus="Update check failed: "+status; loading=false; updateQueued=false; }
        }
        else if(job.kind==Kind::Description) FetchDescription(job.id);
        else FetchArchive(job.id);
    }
}
}
void Start(const std::filesystem::path& packsDirectory) {
    std::lock_guard lock(mutex);
    if(worker.joinable()) return;
    directory=packsDirectory; stopping=false; loading=true; status="Loading the Blish HUD catalog...";
    jobs.push_back({Kind::Catalog,{}});
    worker=std::thread(Run); wake.notify_one();
}
void Stop() {
    { std::lock_guard lock(mutex); stopping=true; jobs.clear(); }
    wake.notify_one(); if(worker.joinable()) worker.join();
    std::lock_guard lock(mutex); entries.clear(); status.clear(); updateStatus.clear(); loading=false; directory.clear();
    stopping=false; updateQueued=false; reloadRequested.store(false);
}
void Refresh() {
    std::lock_guard lock(mutex); if(!worker.joinable() || loading) return;
    loading=true; status="Refreshing the Blish HUD catalog..."; jobs.push_back({Kind::Catalog,{}}); wake.notify_one();
}
void Describe(const std::string& id) {
    std::lock_guard lock(mutex);
    auto* item=Find(id);
    if(!item || item->descriptionPending || item->descriptionReady) return;
    item->descriptionPending=true; jobs.push_back({Kind::Description,id}); wake.notify_one();
}
void Download(const std::string& id) {
    std::lock_guard lock(mutex);
    auto* item=Find(id);
    if(!item || item->downloading) return;
    item->downloading=true; item->downloadStatus="Downloading...";
    jobs.push_back({Kind::Archive,id}); wake.notify_one();
}
void CheckForUpdates(const std::filesystem::path& packsDirectory) {
    std::lock_guard lock(mutex);
    if(!worker.joinable()) {
        directory=packsDirectory; stopping=false;
        worker=std::thread(Run);
    }
    if(updateQueued) return;
    updateStatus=loading ? "Update check queued..." : "Checking installed pack versions...";
    loading=true; updateQueued=true;
    jobs.push_back({Kind::CheckUpdates,{}}); wake.notify_one();
}
std::string GetUpdateStatus() { std::lock_guard lock(mutex); return updateStatus; }
Snapshot GetSnapshot() { std::lock_guard lock(mutex); return {entries,status,loading}; }
bool ConsumeReloadRequest() { return reloadRequested.exchange(false); }
}
