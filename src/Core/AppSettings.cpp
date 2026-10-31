#include "AppSettings.h"
#include "InstalledPacks.h"
#include "AtomicFile.h"
#include <sstream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <map>

namespace { AppSettings::State state; }
AppSettings::State& AppSettings::Get() { return state; }
void AppSettings::Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs) {
    // Missing option in older settings is an explicit opt-out on upgrade/reload.
    state.autoTriggerPieUIMaps=false;
    state.presetsEnabled=false;state.shortcuts={};
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
        if(kind=="shortcutRender"||kind=="shortcutNext"||kind=="shortcutPrevious") {
            Shortcut key;if(!(input>>key.key>>key.ctrl>>key.alt>>key.shift))continue;
            if(key.key>=0&&key.key<256)state.shortcuts[kind=="shortcutRender"?0:kind=="shortcutNext"?1:2]=key;
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
            else if (kind == "showMarkerInfo") state.showMarkerInfo = value;
            else if (kind == "autoTriggerPieUIMaps") state.autoTriggerPieUIMaps = value;
        }
    }
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
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    // A save may run before packs load, or while an archive is temporarily absent.
    // Merge into existing choices so neither case erases the user's selections.
    std::map<std::string,std::map<std::string,bool>> choices;
    std::ifstream previous(directory / "settings.txt");
    if(!previous) {
        const bool exists=std::filesystem::exists(directory / "settings.txt",ec);
        if(exists||ec)return; // Do not overwrite a file whose choices could not be read.
    }
    std::string line;
    while(std::getline(previous,line)) {
        std::istringstream input(line);std::string kind,file,key;bool visible;
        if(input>>kind&&kind=="category"&&input>>std::quoted(file)>>key>>visible)
            choices[file][key]=visible;
    }
    if(previous.bad())return;
    previous.close(); // Release the Windows file handle before atomic replacement.
    for(const auto&pack:packs)for(const auto&[key,category]:pack.categories)
        choices[pack.path.filename().string()][key]=category->visible;
    std::ostringstream output;
    output << "presetsEnabled " << state.presetsEnabled << '\n';
    const char* shortcutNames[]={"shortcutRender","shortcutNext","shortcutPrevious"};
    for(size_t i=0;i<state.shortcuts.size();++i){const auto&k=state.shortcuts[i];output<<shortcutNames[i]<<' '<<k.key<<' '<<k.ctrl<<' '<<k.alt<<' '<<k.shift<<'\n';}
    output << "infoHud " << state.infoHud.saved << ' ' << state.infoHud.x << ' ' << state.infoHud.y << '\n';
    output << "copyHud " << state.copyHud.saved << ' ' << state.copyHud.x << ' ' << state.copyHud.y << '\n';
    output << "nestingStyle " << static_cast<int>(state.nestingStyle) << '\n';
    output << "mapTrailStyle " << static_cast<int>(state.mapTrailStyle) << '\n';
    output << "clipboardMode " << static_cast<int>(state.clipboardMode) << "\nshowMarkerInfo " << state.showMarkerInfo << '\n';
    output << "autoTriggerPieUIMaps " << state.autoTriggerPieUIMaps << '\n';
    output << "world " << state.world << "\nmini " << state.mini << "\nbig " << state.big << '\n';
    output << "miniIconSize " << state.miniIconSize << "\nminiTrailSize " << state.miniTrailSize << '\n';
    output << "bigIconSize " << state.bigIconSize << "\nbigTrailSize " << state.bigTrailSize << '\n';
    output << "worldIconSize " << state.worldIconSize << "\nworldTrailSize " << state.worldTrailSize << '\n';
    output << "playerBubble " << state.playerBubble << "\nbubbleRadius " << state.bubbleRadius
           << "\nbubbleFeather " << state.bubbleFeather << '\n';
    output << "scaleBaselineV1 1\n";
    for(const auto&[file,categories]:choices)for(const auto&[key,visible]:categories)
        output << "category " << std::quoted(file) << ' ' << key << ' ' << visible << '\n';
    AtomicFile::Write(directory / "settings.txt",output.str());
}
