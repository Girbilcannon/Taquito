#pragma once
#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <functional>
#include <limits>
#include "MarkerPack.h"

namespace AppSettings {
    enum class NestingStyle { Taquito = 0, Taco = 1 };
    enum class ClipboardMode { Always = 0, Manual = 1, Never = 2 };
    enum class MapTrailStyle { Taco = 0, BlishHUD = 1 };
    struct HudPosition { bool saved=false;float x=0,y=0; };
    struct Shortcut {int key=0;bool ctrl=false,alt=false,shift=false;};
    struct State {
        bool renderingEnabled=true; // Session-only master switch, shared with map consumers.
        bool presetsEnabled=false;
        bool quickCloseMenu=false;
        std::array<Shortcut,4> shortcuts{};
        HudPosition infoHud,copyHud;
        NestingStyle nestingStyle = NestingStyle::Taquito;
        MapTrailStyle mapTrailStyle = MapTrailStyle::Taco;
        bool world = true, mini = true, big = true;
        bool showMarkerInfo = true;
        bool autoTriggerPieUIMaps = false;
        ClipboardMode clipboardMode = ClipboardMode::Always;
        float miniIconSize = 1.0f, miniTrailSize = 1.0f;
        float bigIconSize = 1.0f, bigTrailSize = 1.0f;
        float worldIconSize = 1.0f, worldTrailSize = 1.0f;
        bool playerBubble = false;
        float bubbleRadius = 2.5f, bubbleFeather = 1.0f;
    };
    State& Get();
    void Flush();
    void QueueFileSave(const std::filesystem::path& path,std::string body);
    void QueueSerializedSave(const std::filesystem::path& path,std::function<std::string()> build,size_t maxBytes=(std::numeric_limits<size_t>::max)());
    bool SaveFailed(const std::filesystem::path& path);
    void Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs);
    void Save(const std::filesystem::path& directory, const std::vector<TacoPack::Pack>& packs);
}
