// Nexus registration and lifecycle. Runtime, pack parsing, settings and UI live in their respective folders.
#include <Windows.h>
#include <cstddef>
#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "Core/AppRuntime.h"
#include "UI/QuickMenu.h"
#include "UI/MarkerInteractions.h"
#include "resource.h"

namespace {
constexpr const char* Name = "Taquito";
constexpr const char* QuickID = "TAQUITO_QUICKACCESS_78FD302C";
constexpr const char* IconID = "TAQUITO_ICON_78FD302C_V2";
constexpr const char* BindID = "KB_TAQUITO_MENU_78FD302C_V2";
constexpr const char* InteractBindID = "KB_TAQUITO_MARKER_INTERACT_78FD302C";
constexpr unsigned IconResource = IDR_TAQUITO_QUICKACCESS_ICON;
AddonDefinition_t definition{};
AddonAPI_t* api = nullptr;
HMODULE module = nullptr;

void OnToggle(const char*, bool released) { if (!released) QuickMenu::RequestToggle(); }
void OnMarkerInteract(const char*, bool released) { if(!released) MarkerInteractions::RequestInteract(); }
void OnMumbleIdentityUpdated(void* value) { AppRuntime::SetMumbleIdentity(static_cast<Mumble::Identity*>(value)); }
void OnRender() { QuickMenu::Render(); }
// Keep registrations below paired with OnUnload before changing the addon lifecycle.
void OnLoad(AddonAPI_t* value) {
    api = value;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void* (*)(std::size_t, void*)>(api->ImguiMalloc),
        reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    AppRuntime::Initialize(api, module);
    QuickMenu::Initialize();
    api->Textures_GetOrCreateFromResource(IconID, IconResource, module);
    api->InputBinds_RegisterWithString(BindID, OnToggle, "ALT+SHIFT+T");
    api->InputBinds_RegisterWithString(InteractBindID, OnMarkerInteract, "ALT+F");
    api->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    api->QuickAccess_Add(QuickID, IconID, IconID, BindID, Name);
    api->GUI_Register(RT_Render, OnRender);
    api->Log(LOGL_INFO, Name, "Taquito 1.0.0.4 loaded.");
}
void OnUnload() {
    if (!api) return;
    api->QuickAccess_Remove(QuickID);
    api->InputBinds_Deregister(BindID);
    api->InputBinds_Deregister(InteractBindID);
    api->Events_Unsubscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    api->GUI_Deregister(OnRender);
    QuickMenu::Shutdown();
    AppRuntime::Shutdown();
    api = nullptr;
}
}
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    definition.Signature = 0x78FD302C; // Preserve this unique Nexus identity across releases.
    definition.APIVersion = NEXUS_API_VERSION;
    definition.Name = Name;
    // Bump this version for releases; never regenerate the signature for an update.
    definition.Version.Major = 1;
    definition.Version.Minor = 0;
    definition.Version.Build = 0;
    definition.Version.Revision = 4;
    definition.Author = "Girbilcannon.8259";
    definition.Description = "Lightweight TacO marker and trail pack viewer.";
    definition.Load = OnLoad;
    definition.Unload = OnUnload;
    definition.Flags = AF_None;
    definition.Provider = UP_GitHub;
    definition.UpdateLink = "https://github.com/Girbilcannon/taquito";
    return &definition;
}
