#pragma once
#include <filesystem>
#include <vector>
#include "MarkerPack.h"

namespace AppSettings {
    enum class NestingStyle { Taquito = 0, Taco = 1 };
    enum class ClipboardMode { Always = 0, Manual = 1, Never = 2 };
    struct State {
        NestingStyle nestingStyle = NestingStyle::Taquito;
        bool world = true, mini = true, big = true;
        bool showMarkerInfo = true;
        ClipboardMode clipboardMode = ClipboardMode::Always;
        float miniIconSize = 1.0f, miniTrailSize = 1.0f;
        float bigIconSize = 0.5f, bigTrailSize = 0.5f;
        float worldIconSize = 2.0f, worldTrailSize = 1.0f;
        bool playerBubble = false;
        float bubbleRadius = 2.5f, bubbleFeather = 1.0f;
    };
    State& Get();
    void Load(const std::filesystem::path& directory, std::vector<TacoPack::Pack>& packs);
    void Save(const std::filesystem::path& directory, const std::vector<TacoPack::Pack>& packs);
}
