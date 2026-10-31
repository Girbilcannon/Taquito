#include "AppSettings.h"
#include "InstalledPacks.h"
#include "AtomicFile.h"
#include <sstream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace { AppSettings::State state; }
AppSettings::State& AppSettings::Get() { return state; }
void AppSettings::Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs) {
    // Missing option in older settings is an explicit opt-out on upgrade/reload.
    state.autoTriggerPieUIMaps=false;
    state.presetsEnabled=false;state.shortcuts={};
    std::ifstream input(directory / "settings.txt");
    // Filename aliases keep category toggles when a tracked pack changes its catalog filename.
    const auto installed=InstalledPacks::Load(directory / "installed-packs.json");
    std::string kind;
    bool normalizedScaleBaseline = false;
    float legacyMini = state.miniIconSize, legacyBig = state.bigIconSize;
    bool hasLegacyMini=false, hasLegacyBig=false;
    bool hasMiniIcon=false, hasMiniTrail=false, hasBigIcon=false, hasBigTrail=false;
    while (input >> kind) {
        if(kind=="shortcutRender"||kind=="shortcutNext"||kind=="shortcutPrevious") {
            Shortcut key;if(!(input>>key.key>>key.ctrl>>key.alt>>key.shift))break;
            if(key.key>=0&&key.key<256)state.shortcuts[kind=="shortcutRender"?0:kind=="shortcutNext"?1:2]=key;
        } else if(kind=="infoHud" || kind=="copyHud") {
            bool saved=false;float x=0,y=0;if(!(input>>saved>>x>>y))break;
            if(std::isfinite(x)&&std::isfinite(y)) {
                auto& hud=kind=="infoHud" ? state.infoHud : state.copyHud;hud={saved,x,y};
            }
        } else if (kind == "category") {
            std::string file, key; bool visible;
            if (!(input >> std::quoted(file) >> key >> visible)) break;
            for (auto& pack : packs) {
                const auto current=pack.path.filename().string();
                bool matches=current==file;
                if(!matches) for(const auto& record:installed) if(record.filename==current &&
                    std::find(record.aliases.begin(),record.aliases.end(),file)!=record.aliases.end()) matches=true;
                if(!matches) continue;
                auto found = pack.categories.find(key);
                if (found != pack.categories.end()) found->second->visible = visible;
            }
        } else if (kind == "nestingStyle") {
            int value=0;
            if (!(input >> value)) break;
            state.nestingStyle=value==1 ? NestingStyle::Taco : NestingStyle::Taquito;
        } else if (kind == "mapTrailStyle") {
            int value=0;if(!(input>>value)) break;
            state.mapTrailStyle=value==1 ? MapTrailStyle::BlishHUD : MapTrailStyle::Taco;
        } else if (kind == "clipboardMode") {
            int value=0;if(!(input>>value)) break;
            state.clipboardMode=static_cast<ClipboardMode>(std::clamp(value,0,2));
        } else if (kind == "scaleBaselineV1") {
            if (!(input >> normalizedScaleBaseline)) break;
        } else if (kind == "miniSize" || kind == "bigSize" || kind == "miniIconSize" || kind == "miniTrailSize" ||
                   kind == "bigIconSize" || kind == "bigTrailSize" || kind == "worldIconSize" || kind == "worldTrailSize" ||
                   kind == "bubbleRadius" || kind == "bubbleFeather") {
            float value;
            if (!(input >> value)) break;
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
            if (!(input >> value)) break;
            if (kind == "world") state.world = value;
            else if (kind == "mini") state.mini = value;
            else if (kind == "big") state.big = value;
            else if (kind == "playerBubble") state.playerBubble = value;
            else if (kind == "presetsEnabled") state.presetsEnabled=value;
            else if (kind == "showMarkerInfo") state.showMarkerInfo = value;
            else if (kind == "autoTriggerPieUIMaps") state.autoTriggerPieUIMaps = value;
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
    for (const auto& pack : packs) for (const auto& [key, category] : pack.categories)
        output << "category " << std::quoted(pack.path.filename().string()) << ' ' << key << ' ' << category->visible << '\n';
    AtomicFile::Write(directory / "settings.txt",output.str());
}
