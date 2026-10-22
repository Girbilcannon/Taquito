#include "QuickMenu.h"
#include "CategoryMenu.h"
#include "OverlayRenderer.h"
#include "MarkerInteractions.h"
#include "PackBrowserWindow.h"
#include "SettingsWindow.h"
#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/MarkerPack.h"
#include "../Core/OnlinePacks.h"
#include "../Core/PathingState.h"
#include "../imgui/imgui.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {
Renderer renderer;
std::vector<TacoPack::Pack> packs;
std::atomic_bool toggleRequested{false};
bool showMenu = false;
std::thread loader;
std::mutex loadMutex;
std::optional<std::vector<TacoPack::Pack>> loadedPacks;
std::atomic_bool loading{false};
bool packsReady=false;
std::vector<InstalledPacks::Record> packCatalog;

void PublishPathingState() {
    PathingState::Update(packs,AppSettings::Get(),AppRuntime::GetPacksDirectory(),packCatalog,packsReady);
}
void Save() { AppSettings::Save(AppRuntime::GetAddonDirectory(), packs); PublishPathingState(); }
// Only the loader thread parses archives; ReceivePacks swaps them onto the render thread.
void Refresh() {
    if (loader.joinable()) loader.join();
    renderer.RequestTextureClear();
    { std::lock_guard lock(loadMutex); loadedPacks.reset(); }
    loading.store(true, std::memory_order_release);
    packsReady=false;
    PublishPathingState();
    auto directory = AppRuntime::GetPacksDirectory();
    loader = std::thread([directory] {
        auto result = TacoPack::LoadFolder(directory);
        { std::lock_guard lock(loadMutex); loadedPacks.emplace(std::move(result)); }
        loading.store(false, std::memory_order_release);
    });
}
void ReceivePacks() {
    if (loading.load(std::memory_order_acquire)) return;
    std::optional<std::vector<TacoPack::Pack>> ready;
    { std::lock_guard lock(loadMutex); ready.swap(loadedPacks); }
    if (!ready) return;
    if (loader.joinable()) loader.join();
    MarkerInteractions::Reset();
    packs = std::move(*ready);
    AppSettings::Load(AppRuntime::GetAddonDirectory(), packs);
    packCatalog=InstalledPacks::Load(AppRuntime::GetAddonDirectory()/"installed-packs.json");
    packsReady=true;
    PublishPathingState();
    if (auto* api = AppRuntime::GetApi()) {
        const auto message = "Loaded " + std::to_string(packs.size()) + " marker pack(s).";
        api->Log(LOGL_INFO, "Taquito", message.c_str());
    }
}
// Show XML categories directly; Save stores visibility for the next launch.
void CategoryChanged() { Save(); renderer.RequestTextureClear(); }
void DrawMenu() {
    if (toggleRequested.exchange(false, std::memory_order_acq_rel)) {
        showMenu = !showMenu;
        if (showMenu) ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always);
    }
    if (!showMenu) return;
    ImGui::SetNextWindowSizeConstraints({250, 50}, {480, ImGui::GetIO().DisplaySize.y * .8f});
    if (ImGui::Begin("Taquito##menu", &showMenu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (loading.load()) ImGui::TextUnformatted("Reading TacoPacks...");
        else if (packs.empty()) ImGui::TextUnformatted("Place .taco or .zip files in TacoPacks beside the DLL.");
        for (auto& pack : packs) {
            const bool tree=AppSettings::Get().nestingStyle==AppSettings::NestingStyle::Taquito;
            if(tree) ImGui::PushID(pack.path.string().c_str());
            for (auto& category : pack.root.children)
                CategoryMenu::Draw(*category, AppSettings::Get().nestingStyle, CategoryChanged,MarkerInteractions::CopyCategory);
            if(tree) ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Download Marker Packs...")) PackBrowserWindow::Open();
        if (ImGui::MenuItem("Check for pack updates")) OnlinePacks::CheckForUpdates(AppRuntime::GetPacksDirectory());
        const auto updateStatus=OnlinePacks::GetUpdateStatus();
        if(!updateStatus.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s",updateStatus.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::MenuItem("Reload packs")) Refresh();
        ImGui::Separator();
        auto* api=AppRuntime::GetApi();
        auto* icon=api && api->Textures_Get ? api->Textures_Get("ICON_OPTIONS") : nullptr;
        if(icon && icon->Resource) {
            ImGui::PushID("TaquitoSettingsIcon");
            if(ImGui::ImageButton(icon->Resource,{18,18},{0,0},{1,1},0)) SettingsWindow::Open();
            ImGui::PopID();
            ImGui::SameLine();
        }
        if(ImGui::MenuItem("Settings")) SettingsWindow::Open();
    }
    ImGui::End();
}
}

void QuickMenu::Initialize() {
    AppSettings::Load(AppRuntime::GetAddonDirectory(),packs);
    auto* api=AppRuntime::GetApi();
    PathingState::Initialize(api ? api->Events_Raise : nullptr);
    renderer.Initialize(api); Refresh();
}
void QuickMenu::Shutdown() { OnlinePacks::Stop(); if (loader.joinable()) loader.join(); Save(); MarkerInteractions::Reset(); renderer.Shutdown(); packs.clear(); showMenu = false; PackBrowserWindow::Close(); SettingsWindow::Close(); { std::lock_guard lock(loadMutex); loadedPacks.reset(); } }
void QuickMenu::RequestToggle() { toggleRequested.store(true, std::memory_order_release); }
void QuickMenu::Render() {
    if(OnlinePacks::ConsumeReloadRequest()) Refresh();
    ReceivePacks();
    auto& settings = AppSettings::Get();
    auto* playableMumble=AppRuntime::GetPlayableMumble();
    renderer.Draw(packs, playableMumble, AppRuntime::GetMumbleIdentity(),
                  settings.world, settings.mini, settings.big, settings.miniIconSize, settings.bigIconSize,
                  settings.miniTrailSize, settings.bigTrailSize,
                  settings.worldIconSize, settings.worldTrailSize,
                  settings.playerBubble, settings.bubbleRadius, settings.bubbleFeather,
                  settings.mapTrailStyle==AppSettings::MapTrailStyle::BlishHUD);
    MarkerInteractions::Render(packs,playableMumble,settings.world);
    DrawMenu();
    PackBrowserWindow::Render();
    SettingsWindow::Render(packs,renderer,loading.load(),Refresh,Save);
}
