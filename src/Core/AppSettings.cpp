#include "AppSettings.h"
#include "InstalledPacks.h"
#include "AtomicFile.h"
#include <sstream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <set>

namespace {
AppSettings::State state;
using Choices=std::map<std::string,std::map<std::string,bool>>;
Choices cachedChoices;std::filesystem::path cachedDirectory;bool cacheReady=false,cacheWritable=true;
// The worker serializes owned snapshots; it never reads live settings or pack pointers.
class SettingsWriter {
    std::mutex mutex;std::condition_variable cv;std::thread worker;
    struct Job{std::function<std::string()> build;size_t maxBytes;};
    std::map<std::filesystem::path,Job> pending;std::map<std::filesystem::path,std::string> written;bool busy=false,stop=false;
    std::set<std::filesystem::path> failed;
    void Run(){
        std::unique_lock lock(mutex);
        for(;;){cv.wait(lock,[&]{return stop||!pending.empty();});if(stop&&pending.empty())break;
            auto job=pending.extract(pending.begin());busy=true;lock.unlock();
            bool saved=false;
            try{auto body=job.mapped().build();if(body.size()<=job.mapped().maxBytes){
                const auto last=written.find(job.key());
                saved=last!=written.end()&&last->second==body;
                if(!saved)saved=AtomicFile::Write(job.key(),body);
                if(saved)written[job.key()]=std::move(body);
            }}catch(...){saved=false;}
            lock.lock();if(saved)failed.erase(job.key());else failed.insert(job.key());busy=false;cv.notify_all();
        }
    }
public:
    ~SettingsWriter(){Flush();}
    bool Failed(const std::filesystem::path&path){std::lock_guard lock(mutex);return failed.count(path)!=0;}
    void Queue(const std::filesystem::path&path,std::function<std::string()> build,size_t maxBytes){
        std::lock_guard lock(mutex);pending[path]={std::move(build),maxBytes};
        if(!worker.joinable()){stop=false;worker=std::thread([this]{Run();});}cv.notify_all();
    }
    void Flush(){
        std::unique_lock lock(mutex);if(!worker.joinable())return;
        cv.wait(lock,[&]{return pending.empty()&&!busy;});stop=true;cv.notify_all();lock.unlock();worker.join();written.clear();
    }
};
SettingsWriter writer;
}
void AppSettings::Flush(){writer.Flush();}
void AppSettings::QueueFileSave(const std::filesystem::path&path,std::string body){QueueSerializedSave(path,[body=std::move(body)]{return body;});}
void AppSettings::QueueSerializedSave(const std::filesystem::path&path,std::function<std::string()> build,size_t maxBytes){writer.Queue(path,std::move(build),maxBytes);}
bool AppSettings::SaveFailed(const std::filesystem::path&path){return writer.Failed(path);}
AppSettings::State& AppSettings::Get() { return state; }
void AppSettings::Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs) {
    Flush();
    // Missing option in older settings is an explicit opt-out on upgrade/reload.
    state.autoTriggerPieUIMaps=false;
    state.presetsEnabled=false;state.quickCloseMenu=false;state.shortcuts={};
    std::ifstream fileInput(directory / "settings.txt");
    // Filename aliases keep category toggles when a tracked pack changes its catalog filename.
    const auto installed=InstalledPacks::Load(directory / "installed-packs.json");
    std::string kind;
    bool normalizedScaleBaseline = false;
    float legacyMini = state.miniIconSize, legacyBig = state.bigIconSize;
    bool hasLegacyMini=false, hasLegacyBig=false;
    bool hasMiniIcon=false, hasMiniTrail=false, hasBigIcon=false, hasBigTrail=false;
    std::map<std::string,std::map<std::string,bool>> choices;
    std::string line;
    while (std::getline(fileInput,line)) {
        std::istringstream input(line);
        if(!(input>>kind))continue;
        if(kind=="shortcutRender"||kind=="shortcutNext"||kind=="shortcutPrevious"||kind=="shortcutMenu") {
            Shortcut key;if(!(input>>key.key>>key.ctrl>>key.alt>>key.shift))continue;
            if(key.key>=0&&key.key<256)state.shortcuts[kind=="shortcutRender"?0:kind=="shortcutNext"?1:kind=="shortcutPrevious"?2:3]=key;
        } else if(kind=="infoHud" || kind=="copyHud") {
            bool saved=false;float x=0,y=0;if(!(input>>saved>>x>>y))continue;
            if(std::isfinite(x)&&std::isfinite(y)) {
                auto& hud=kind=="infoHud" ? state.infoHud : state.copyHud;hud={saved,x,y};
            }
        } else if (kind == "category") {
            std::string file, key; bool visible;
            if (!(input >> std::quoted(file) >> key >> visible)) continue;
            choices[file][key]=visible;
        } else if (kind == "nestingStyle") {
            int value=0;
            if (!(input >> value)) continue;
            state.nestingStyle=value==1 ? NestingStyle::Taco : NestingStyle::Taquito;
        } else if (kind == "mapTrailStyle") {
            int value=0;if(!(input>>value)) continue;
            state.mapTrailStyle=value==1 ? MapTrailStyle::BlishHUD : MapTrailStyle::Taco;
        } else if (kind == "clipboardMode") {
            int value=0;if(!(input>>value)) continue;
            state.clipboardMode=static_cast<ClipboardMode>(std::clamp(value,0,2));
        } else if (kind == "scaleBaselineV1") {
            if (!(input >> normalizedScaleBaseline)) continue;
        } else if (kind == "miniSize" || kind == "bigSize" || kind == "miniIconSize" || kind == "miniTrailSize" ||
                   kind == "bigIconSize" || kind == "bigTrailSize" || kind == "worldIconSize" || kind == "worldTrailSize" ||
                   kind == "bubbleRadius" || kind == "bubbleFeather") {
            float value;
            if (!(input >> value)) continue;
            value = std::clamp(value, 0.25f, kind == "bubbleRadius" ? 8.0f : 3.0f);
            if (kind == "miniSize") { legacyMini=value; hasLegacyMini=true; }
            else if (kind == "bigSize") { legacyBig=value; hasLegacyBig=true; }
            else if (kind == "miniIconSize") { state.miniIconSize=value; hasMiniIcon=true; }
            else if (kind == "miniTrailSize") { state.miniTrailSize=value; hasMiniTrail=true; }
            else if (kind == "bigIconSize") { state.bigIconSize=value; hasBigIcon=true; }
            else if (kind == "bigTrailSize") { state.bigTrailSize=value; hasBigTrail=true; }
            else if (kind == "worldIconSize") state.worldIconSize = value;
            else if (kind == "worldTrailSize") state.worldTrailSize = value;
            else if (kind == "bubbleRadius") state.bubbleRadius = value;
            else state.bubbleFeather = value;
        } else {
            bool value;
            if (!(input >> value)) continue;
            if (kind == "world") state.world = value;
            else if (kind == "mini") state.mini = value;
            else if (kind == "big") state.big = value;
            else if (kind == "playerBubble") state.playerBubble = value;
            else if (kind == "presetsEnabled") state.presetsEnabled=value;
            else if (kind == "quickCloseMenu") state.quickCloseMenu=value;
            else if (kind == "showMarkerInfo") state.showMarkerInfo = value;
            else if (kind == "autoTriggerPieUIMaps") state.autoTriggerPieUIMaps = value;
        }
    }
    std::error_code readError;
    cacheWritable=!fileInput.bad()&&(fileInput.is_open()||!std::filesystem::exists(directory/"settings.txt",readError))&&!readError;
    cachedChoices=choices;cachedDirectory=directory;cacheReady=true;
    // Current filenames take precedence over older aliases, regardless of file order.
    for(auto&pack:packs) {
        const auto current=pack.path.filename().string();
        std::vector<std::string> names{current};
        for(const auto&record:installed)if(record.filename==current)
            names.insert(names.end(),record.aliases.begin(),record.aliases.end());
        for(const auto&[key,category]:pack.categories)for(const auto&name:names) {
            auto file=choices.find(name);if(file==choices.end())continue;
            auto value=file->second.find(key);if(value==file->second.end())continue;
            category->visible=value->second;break;
        }
    }
    // Old combined scales initialize both controls; explicit new values win in any order.
    if(hasLegacyMini) {
        if(!hasMiniIcon) state.miniIconSize=legacyMini;
        if(!hasMiniTrail) state.miniTrailSize=legacyMini;
    }
    if(hasLegacyBig) {
        if(!hasBigIcon) state.bigIconSize=legacyBig;
        if(!hasBigTrail) state.bigTrailSize=legacyBig;
    }
    // Establish one neutral baseline on upgrade; subsequent user adjustments persist.
    if(!normalizedScaleBaseline) {
        state.miniIconSize=state.miniTrailSize=state.bigIconSize=state.bigTrailSize=1.0f;
        state.worldIconSize=state.worldTrailSize=1.0f;
    }
}
void AppSettings::Save(const std::filesystem::path& directory, const std::vector<TacoPack::Pack>& packs) {
    // Load once, then merge in memory. An empty/partial pack list cannot erase saved choices.
    if(!cacheReady||cachedDirectory!=directory){std::vector<TacoPack::Pack> none;auto current=state;Load(directory,none);state=current;}
    if(!cacheWritable)return;
    auto&choices=cachedChoices;
    for(const auto&pack:packs){const auto file=pack.path.filename().string();for(const auto&[key,category]:pack.categories)
        choices[file][key]=category->visible;}
    QueueSerializedSave(directory/"settings.txt",[choices=cachedChoices,settings=state]{
    std::ostringstream output;
    output << "presetsEnabled " << settings.presetsEnabled << '\n';
    output << "quickCloseMenu " << settings.quickCloseMenu << '\n';
    const char* shortcutNames[]={"shortcutRender","shortcutNext","shortcutPrevious","shortcutMenu"};
    for(size_t i=0;i<settings.shortcuts.size();++i){const auto&k=settings.shortcuts[i];output<<shortcutNames[i]<<' '<<k.key<<' '<<k.ctrl<<' '<<k.alt<<' '<<k.shift<<'\n';}
    output << "infoHud " << settings.infoHud.saved << ' ' << settings.infoHud.x << ' ' << settings.infoHud.y << '\n';
    output << "copyHud " << settings.copyHud.saved << ' ' << settings.copyHud.x << ' ' << settings.copyHud.y << '\n';
    output << "nestingStyle " << static_cast<int>(settings.nestingStyle) << '\n';
    output << "mapTrailStyle " << static_cast<int>(settings.mapTrailStyle) << '\n';
    output << "clipboardMode " << static_cast<int>(settings.clipboardMode) << "\nshowMarkerInfo " << settings.showMarkerInfo << '\n';
    output << "autoTriggerPieUIMaps " << settings.autoTriggerPieUIMaps << '\n';
    output << "world " << settings.world << "\nmini " << settings.mini << "\nbig " << settings.big << '\n';
    output << "miniIconSize " << settings.miniIconSize << "\nminiTrailSize " << settings.miniTrailSize << '\n';
    output << "bigIconSize " << settings.bigIconSize << "\nbigTrailSize " << settings.bigTrailSize << '\n';
    output << "worldIconSize " << settings.worldIconSize << "\nworldTrailSize " << settings.worldTrailSize << '\n';
    output << "playerBubble " << settings.playerBubble << "\nbubbleRadius " << settings.bubbleRadius
           << "\nbubbleFeather " << settings.bubbleFeather << '\n';
    output << "scaleBaselineV1 1\n";
    for(const auto&[file,categories]:choices)for(const auto&[key,visible]:categories)
        output << "category " << std::quoted(file) << ' ' << key << ' ' << visible << '\n';
    return output.str();
    });
}
