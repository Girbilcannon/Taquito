#include "QuickMenu.h"
#include "CategoryMenu.h"
#include "OverlayRenderer.h"
#include "MarkerInteractions.h"
#include "PackBrowserWindow.h"
#include "SettingsWindow.h"
#include "ShortcutSettings.h"
#include "PresetsWindow.h"
#include "../Core/Presets.h"
#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/MarkerPack.h"
#include "../Core/OnlinePacks.h"
#include "../Core/PathingState.h"
#include "../Core/AttributeRuntime.h"
#include "../Core/CheckpointRuntime.h"
#include "../third_party/jsonxx.h"
#include <chrono>
#include <algorithm>
#include "../imgui/imgui.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {
Renderer renderer;
std::vector<TacoPack::Pack> packs;
std::atomic_bool toggleRequested{false},iconToggleRequested{false};
bool leftCycleSeen=false,menuAtLeftPress=false;int lastLeftReleaseFrame=-100;
AppSettings::State lastSharedSettings;
bool showMenu = false;
std::thread loader;
std::mutex loadMutex;
std::optional<std::vector<TacoPack::Pack>> loadedPacks;
std::atomic_bool loading{false};
bool packsReady=false;
std::vector<InstalledPacks::Record> packCatalog;

void PublishPathingState() {
    PathingState::Update(packs,AppSettings::Get(),AppRuntime::GetPacksDirectory(),packCatalog,packsReady);
    lastSharedSettings=AppSettings::Get();
}
void ResetSelectionContact() {
    MarkerInteractions::Reset();AttributeRuntime::ResetFocus();CheckpointRuntime::UpdateContact(0,{},false);
}
void CategoryChanged();
void Save() {
    // Local preferences do not capture presets, touch categories, or rebuild the bridge snapshot.
    if(packsReady&&Presets::Enabled()!=AppSettings::Get().presetsEnabled){
        if(!Presets::SetEnabled(AppSettings::Get().presetsEnabled,packs))AppSettings::Get().presetsEnabled=false;
        ResetSelectionContact();Presets::Save();CategoryChanged();return;
    }
    AppSettings::Save(AppRuntime::GetAddonDirectory(),{});
    const auto&s=AppSettings::Get();const auto&old=lastSharedSettings;
    if(s.renderingEnabled!=old.renderingEnabled||s.miniIconSize!=old.miniIconSize||s.miniTrailSize!=old.miniTrailSize||
       s.bigIconSize!=old.bigIconSize||s.bigTrailSize!=old.bigTrailSize)PublishPathingState();
}
void PresetChanged(){ResetSelectionContact();CategoryChanged();}
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
    AttributeRuntime::ResetFocus();
    CheckpointRuntime::PrepareReload();
    packs = std::move(*ready);
    AppSettings::Load(AppRuntime::GetAddonDirectory(), packs);
    if(!Presets::SetEnabled(AppSettings::Get().presetsEnabled,packs))AppSettings::Get().presetsEnabled=false;
    Presets::Apply(packs);Presets::Save();
    CheckpointRuntime::Load(packs);
    packCatalog=InstalledPacks::Load(AppRuntime::GetAddonDirectory()/"installed-packs.json");
    packsReady=true;
    PublishPathingState();
    if (auto* api = AppRuntime::GetApi()) {
        const auto message = "Loaded " + std::to_string(packs.size()) + " marker pack(s).";
        api->Log(LOGL_INFO, "Taquito", message.c_str());
    }
}
// Show XML categories directly; Save stores visibility for the next launch.
void CategoryChanged() {
    if(packsReady&&Presets::Enabled()){Presets::Capture(packs);Presets::Save();}
    AppSettings::Save(AppRuntime::GetAddonDirectory(),packs);PublishPathingState();
}
void DrawMenu() {
    bool justOpened=false;
    const bool normalToggle=toggleRequested.exchange(false,std::memory_order_acq_rel);
    const bool iconToggle=iconToggleRequested.exchange(false,std::memory_order_acq_rel);
    if (normalToggle||iconToggle) {
        const bool sameMouseCycle=leftCycleSeen&&((ShortcutSettings::MousePressed()&1u)||ImGui::GetFrameCount()-lastLeftReleaseFrame<=2);
        showMenu=iconToggle&&sameMouseCycle ? !menuAtLeftPress : !showMenu;
        if(iconToggle)leftCycleSeen=false;
        justOpened=showMenu;
    }
    if (!showMenu) return;
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_FirstUseEver);
    const auto screen=ImGui::GetIO().DisplaySize;
    const ImVec2 limit{std::max(1.0f,screen.x-16),std::max(1.0f,screen.y-16)};
    ImGui::SetNextWindowSizeConstraints({std::min(250.0f,limit.x),0},limit);
    if (ImGui::Begin("Taquito##menu", &showMenu,ImGuiWindowFlags_AlwaysAutoResize)) {
        if (loading.load()) ImGui::TextUnformatted("Reading TacoPacks...");
        else if (packs.empty()) ImGui::TextUnformatted("Place .taco or .zip files in TacoPacks beside the DLL.");
        if(!AppSettings::Get().renderingEnabled){
            ImGui::TextColored({1,.2f,.2f,1},"Rendering Disabled");
            if(ImGui::BeginPopupContextItem("##RenderingState")){
                if(ImGui::MenuItem("Enable Rendering")){AppSettings::Get().renderingEnabled=true;Save();}
                ImGui::EndPopup();
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*(AppSettings::Get().renderingEnabled?1.0f:.35f));
        for (auto& pack : packs) {
            const bool tree=AppSettings::Get().nestingStyle==AppSettings::NestingStyle::Taquito;
            if(tree) ImGui::PushID(pack.path.string().c_str());
            for (auto& category : pack.root.children)
                CategoryMenu::Draw(*category, AppSettings::Get().nestingStyle, CategoryChanged,MarkerInteractions::CopyCategory);
            if(tree) ImGui::PopID();
        }
        ImGui::PopStyleVar();
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
        if(AppSettings::Get().presetsEnabled&&Presets::Enabled())if(ImGui::MenuItem("Presets"))PresetsWindow::Open();
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
    if(AppSettings::Get().quickCloseMenu&&!justOpened&&
       ShortcutSettings::MouseReleased()&&
       !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows|ImGuiHoveredFlags_AllowWhenBlockedByPopup|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))showMenu=false;
    const auto size=ImGui::GetWindowSize(),pos=ImGui::GetWindowPos();
    ImGui::SetWindowPos({std::clamp(pos.x,0.0f,std::max(0.0f,screen.x-size.x)),std::clamp(pos.y,0.0f,std::max(0.0f,screen.y-size.y))});
    ImGui::End();
}
}

void QuickMenu::Initialize() {
    AppSettings::Load(AppRuntime::GetAddonDirectory(),packs);
    AppSettings::Get().renderingEnabled=true;
    Presets::Initialize(AppRuntime::GetAddonDirectory(),AppSettings::Get().presetsEnabled);
    ShortcutSettings::Reset();
    auto* api=AppRuntime::GetApi();
    PathingState::Initialize(api ? api->Events_Raise : nullptr);
    AttributeRuntime::Initialize(AppRuntime::GetAddonDirectory());
    CheckpointRuntime::Clear();
    renderer.Initialize(api); Refresh();
}
void QuickMenu::Shutdown() { CheckpointRuntime::Clear(); OnlinePacks::Stop(); if (loader.joinable()) loader.join(); Save(); CategoryChanged(); AppSettings::Flush(); MarkerInteractions::Reset(); renderer.Shutdown(); packs.clear(); showMenu = false; PackBrowserWindow::Close(); SettingsWindow::Close(); PresetsWindow::Close();Presets::Clear();ShortcutSettings::Reset(); { std::lock_guard lock(loadMutex); loadedPacks.reset(); } }
void QuickMenu::RequestToggle() { toggleRequested.store(true, std::memory_order_release); }
void QuickMenu::RequestIconToggle(){iconToggleRequested.store(true,std::memory_order_release);}
void QuickMenu::Render() {
    if(OnlinePacks::ConsumeReloadRequest()) Refresh();
    ReceivePacks();
    auto& settings = AppSettings::Get();
    auto* rawMumble=AppRuntime::GetMumble();
    const bool focused=rawMumble&&rawMumble->UITick&&rawMumble->Context.IsGameFocused;
    const auto shortcut=ShortcutSettings::Poll(focused,focused&&!rawMumble->Context.IsTextboxFocused);
    if(ShortcutSettings::MousePressed()&1u){leftCycleSeen=true;menuAtLeftPress=showMenu;}
    if(ShortcutSettings::MouseReleased()&1u)lastLeftReleaseFrame=ImGui::GetFrameCount();
    bool shortcutsChanged=shortcut.changed;
    if(shortcut.actions&1u){settings.renderingEnabled=!settings.renderingEnabled;shortcutsChanged=true;}
    if(shortcut.actions&8u)QuickMenu::RequestToggle();
    if(packsReady&&((shortcut.actions&2u&&Presets::Step(1,packs))||(shortcut.actions&4u&&Presets::Step(-1,packs)))){PresetChanged();}
    if(shortcutsChanged)Save();
    auto* playableMumble=AppRuntime::GetPlayableMumble();
    static std::wstring lastIdentity;static std::string character;
    if(playableMumble) {
        const auto* raw=playableMumble->Identity;size_t length=0;while(length<256 && raw[length])++length;
        std::wstring identity(raw,length);
        if(identity!=lastIdentity) {
            lastIdentity=identity;character.clear();
            const int bytes=WideCharToMultiByte(CP_UTF8,0,identity.data(),static_cast<int>(identity.size()),nullptr,0,nullptr,nullptr);
            std::string json(static_cast<size_t>(std::max(0,bytes)),0);
            if(bytes>0)WideCharToMultiByte(CP_UTF8,0,identity.data(),static_cast<int>(identity.size()),json.data(),bytes,nullptr,nullptr);
            jsonxx::Object parsed;if(parsed.parse(json)&&parsed.has<jsonxx::String>("name"))character=parsed.get<jsonxx::String>("name");
        }
    }
    const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    AttributeRuntime::Tick(playableMumble ? playableMumble->Context.MapID : 0,
        playableMumble ? playableMumble->Context.ShardID : 0,playableMumble ? playableMumble->Context.InstanceID : 0,
        playableMumble ? character : std::string{},playableMumble ? static_cast<int>(playableMumble->Context.MountIndex) : -1,
        now,ImGui::GetTime());
    const TacoPack::Vec3 player=playableMumble ? TacoPack::Vec3{playableMumble->AvatarPosition.X,playableMumble->AvatarPosition.Y,playableMumble->AvatarPosition.Z} : TacoPack::Vec3{};
    AttributeRuntime::UpdateFocus(packs,player,playableMumble && settings.renderingEnabled && settings.world && !playableMumble->Context.IsMapOpen);
    MarkerInteractions::Render(packs,playableMumble,settings.renderingEnabled&&settings.world,Save,CategoryChanged);
    const bool checkpointContact=playableMumble && settings.renderingEnabled && settings.world && playableMumble->Context.IsGameFocused &&
        !playableMumble->Context.IsMapOpen && !playableMumble->Context.IsTextboxFocused && !ImGui::GetIO().WantTextInput;
    CheckpointRuntime::UpdateContact(playableMumble ? playableMumble->Context.MapID : 0,player,checkpointContact);
    renderer.Draw(packs, playableMumble, AppRuntime::GetMumbleIdentity(),
                  settings.renderingEnabled&&settings.world, settings.renderingEnabled&&settings.mini, settings.renderingEnabled&&settings.big, settings.miniIconSize, settings.bigIconSize,
                  settings.miniTrailSize, settings.bigTrailSize,
                  settings.worldIconSize, settings.worldTrailSize,
                  settings.playerBubble, settings.bubbleRadius, settings.bubbleFeather,
                  settings.mapTrailStyle==AppSettings::MapTrailStyle::BlishHUD);
    DrawMenu();
    PackBrowserWindow::Render();
    if(!settings.presetsEnabled)PresetsWindow::Close();
    PresetsWindow::Render(packs,PresetChanged);
    SettingsWindow::Render(packs,renderer,loading.load(),Refresh,Save);
}
