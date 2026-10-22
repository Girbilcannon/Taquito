#include "AppRuntime.h"
#include <atomic>
#include <vector>

namespace {
AddonAPI_t* api = nullptr;
Mumble::Data* mumble = nullptr;
std::atomic<Mumble::Identity*> identity{nullptr};
std::filesystem::path addonDirectory;
std::filesystem::path packsDirectory;
}

void AppRuntime::Initialize(AddonAPI_t* value, HMODULE addonModule) {
    api = value;
    mumble = static_cast<Mumble::Data*>(api->DataLink_Get(DL_MUMBLE_LINK));
    identity.store(static_cast<Mumble::Identity*>(api->DataLink_Get(DL_MUMBLE_LINK_IDENTITY)));
    const char* path = api->Paths_GetAddonDirectory("Taquito");
    addonDirectory = path ? std::filesystem::path(path) : std::filesystem::path("addons") / "Taquito";
    // TacoPacks lives beside the loaded DLL; addonDirectory holds Taquito's own settings.
    std::vector<wchar_t> modulePath(32768);
    const DWORD length = GetModuleFileNameW(addonModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length > 0 && length < modulePath.size())
        packsDirectory = std::filesystem::path(std::wstring(modulePath.data(), length)).parent_path() / L"TacoPacks";
    else
        packsDirectory = addonDirectory / "TacoPacks";
    std::error_code error;
    std::filesystem::create_directories(addonDirectory, error);
    if (error) api->Log(LOGL_WARNING, "Taquito", "Could not create Taquito settings directory.");
    error.clear();
    std::filesystem::create_directories(packsDirectory, error);
    if (error) api->Log(LOGL_WARNING, "Taquito", "Could not create TacoPacks beside the addon DLL.");
}
void AppRuntime::Shutdown() { identity.store(nullptr); mumble = nullptr; api = nullptr; addonDirectory.clear(); packsDirectory.clear(); }
AddonAPI_t* AppRuntime::GetApi() { return api; }
Mumble::Data* AppRuntime::GetMumble() { return mumble; }
Mumble::Data* AppRuntime::GetPlayableMumble() {
    auto* data=GetMumble();
    if(!data || !data->UITick || !data->Context.MapID) return nullptr;
    if(api && api->DataLink_Get) {
        const auto* link=static_cast<const NexusLinkData_t*>(api->DataLink_Get(DL_NEXUS_LINK));
        // Nexus can identify loading before Mumble clears a stale map ID.
        if(link && !link->IsGameplay) return nullptr;
    }
    return data;
}
Mumble::Identity* AppRuntime::GetMumbleIdentity() {
    auto* value = identity.load();
    if (!value && api) {
        value = static_cast<Mumble::Identity*>(api->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));
        identity.store(value);
    }
    return value;
}
void AppRuntime::SetMumbleIdentity(Mumble::Identity* value) { identity.store(value); }
const std::filesystem::path& AppRuntime::GetAddonDirectory() { return addonDirectory; }
const std::filesystem::path& AppRuntime::GetPacksDirectory() { return packsDirectory; }
