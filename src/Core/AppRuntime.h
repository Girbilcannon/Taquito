#pragma once
#include <filesystem>
#include "../mumble/Mumble.h"
#include "../nexus/Nexus.h"

namespace AppRuntime {
    void Initialize(AddonAPI_t* api, HMODULE addonModule);
    void Shutdown();
    AddonAPI_t* GetApi();
    Mumble::Data* GetMumble();
    // Null during loading/character selection or when there is no valid map.
    Mumble::Data* GetPlayableMumble();
    Mumble::Identity* GetMumbleIdentity();
    void SetMumbleIdentity(Mumble::Identity* value);
    const std::filesystem::path& GetAddonDirectory();
    const std::filesystem::path& GetPacksDirectory();
}
