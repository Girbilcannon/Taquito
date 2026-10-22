#pragma once
#include "AppSettings.h"
#include "InstalledPacks.h"

namespace PathingState {
using RaiseEvent=void (*)(const char*,void*);
void Initialize(RaiseEvent raise);
// Called on the UI thread; archive workers never expose partially built packs.
void Update(const std::vector<TacoPack::Pack>& packs,const AppSettings::State& settings,
            const std::filesystem::path& directory,const std::vector<InstalledPacks::Record>& catalog,bool ready);
// Any event thread may request a copy of the last complete snapshot.
void Reply();
void Rebroadcast();
struct Diagnostics {
    bool initialized=false,active=false,ready=false;
    uint64_t revision=0,events=0,requests=0;
    size_t packs=0;
    size_t loadedPacks=0;
    bool minimap=false;
};
Diagnostics GetDiagnostics();
std::string GetSnapshot();
void Shutdown();
}
