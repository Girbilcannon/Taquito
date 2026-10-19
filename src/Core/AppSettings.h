#pragma once
#include <filesystem>
#include <vector>
#include "MarkerPack.h"

namespace AppSettings {
    enum class NestingStyle { Taquito = 0, Taco = 1 };
    enum class ClipboardMode { Always = 0, Manual = 1, Never = 2 };
    enum class MapTrailStyle { Taco = 0, BlishHUD = 1 };
    struct State {
        NestingStyle nestingStyle = NestingStyle::Taquito;
        MapTrailStyle mapTrailStyle = MapTrailStyle::Taco;
        bool world = true, mini = true, big = true;
        bool showMarkerInfo = true;
        ClipboardMode clipboardMode = ClipboardMode::Always;
        float miniIconSize = 1.0f, miniTrailSize = 1.0f;
        float bigIconSize = 1.0f, bigTrailSize = 1.0f;
        float worldIconSize = 1.0f, worldTrailSize = 1.0f;
        bool playerBubble = false;
        float bubbleRadius = 2.5f, bubbleFeather = 1.0f;
    };
    State& Get();
    void Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs);
    void Save(const std::filesystem::path& directory, const std::vector<TacoPack::Pack>& packs);
}
