#include "SettingsWindow.h"
#include "PackBrowserWindow.h"
#include "OverlayRenderer.h"
#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/PathingState.h"
#include "../Core/OnlinePacks.h"
#include "../Core/AttributeRuntime.h"
#include "../Core/CheckpointRuntime.h"
#include "../TaquitoAPI.h"
#include "../imgui/imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <cmath>
#include <cfloat>

#pragma comment(lib, "shell32.lib")

namespace {
bool shown=false;
bool resetDiagnostics=true;
}
void SettingsWindow::Open() { shown=true; resetDiagnostics=true; }
void SettingsWindow::Close() { shown=false; }
void SettingsWindow::Render(const std::vector<TacoPack::Pack>& packs, const Renderer& renderer,
                            bool loading, void (*reload)(), void (*save)()) {
    if(!shown) return;
    ImGui::SetNextWindowSize({620,440},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({520,320},{FLT_MAX,FLT_MAX});
    if(!ImGui::Begin("Taquito Settings##Taquito",&shown,ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }
    const auto& folder=AppRuntime::GetPacksDirectory();
    auto& settings=AppSettings::Get();
    bool changed=false;
    ImGui::TextDisabled("Taquito " TAQUITO_VERSION);
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Display",ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("In-world markers and trails",&settings.world);
        changed |= ImGui::Checkbox("Minimap markers and trails",&settings.mini);
        changed |= ImGui::Checkbox("Full map markers and trails",&settings.big);
        ImGui::TextWrapped("These display switches affect Taquito only. Pie UI controls its own map visibility.");
        ImGui::Spacing();
        int trailStyle=static_cast<int>(settings.mapTrailStyle);
        if(ImGui::Combo("Map trail style",&trailStyle,"Taco\0Blish HUD\0\0")) {
            settings.mapTrailStyle=static_cast<AppSettings::MapTrailStyle>(trailStyle);changed=true;
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip(settings.mapTrailStyle==AppSettings::MapTrailStyle::BlishHUD ?
            "Solid lines use texture-derived colors or the XML color. Width stays constant as you zoom; 1.00x is 4 pixels at normal UI scale." :
            "Textured trails use the pack's map sizing and zoom attributes.");
        ImGui::Spacing();
    }
    if(ImGui::CollapsingHeader("Scale")) {
        ImGui::TextDisabled("In world");
        changed |= ImGui::SliderFloat("Marker scale##World",&settings.worldIconSize,0.25f,3.0f,"%.2fx");
        changed |= ImGui::SliderFloat("Trail width##World",&settings.worldTrailSize,0.25f,3.0f,"%.2fx");
        ImGui::Spacing();
        ImGui::TextDisabled("Minimap");
        changed |= ImGui::SliderFloat("Icon scale##Mini",&settings.miniIconSize,0.25f,3.0f,"%.2fx");
        changed |= ImGui::SliderFloat("Trail width##Mini",&settings.miniTrailSize,0.25f,3.0f,"%.2fx");
        ImGui::Spacing();
        ImGui::TextDisabled("Full map");
        changed |= ImGui::SliderFloat("Icon scale##Full",&settings.bigIconSize,0.25f,3.0f,"%.2fx");
        changed |= ImGui::SliderFloat("Trail width##Full",&settings.bigTrailSize,0.25f,3.0f,"%.2fx");
        ImGui::TextWrapped("1.00x uses normalized pack sizes. Blish HUD map lines use 4 pixels at normal UI scale.");
        if(ImGui::Button("Reset all scales to 1.00x")) {
            settings.worldIconSize=settings.worldTrailSize=settings.miniIconSize=settings.miniTrailSize=
                settings.bigIconSize=settings.bigTrailSize=1.0f;
            changed=true;
        }
        ImGui::Spacing();
    }
    if(ImGui::CollapsingHeader("Marker Interaction")) {
        changed |= ImGui::Checkbox("Show marker information",&settings.showMarkerInfo);
        int clipboard=static_cast<int>(settings.clipboardMode);
        if(ImGui::Combo("Marker clipboard",&clipboard,"Always (including auto-trigger)\0Only when interacted with\0Never\0\0")) {
            settings.clipboardMode=static_cast<AppSettings::ClipboardMode>(clipboard);changed=true;
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Nearby marker copy: click Copy text or use the Taquito marker interaction keybind (default Alt+F). Change the keybind in Nexus settings.");
        changed |= ImGui::Checkbox("Enable Auto-Trigger of Pie UI Maps",&settings.autoTriggerPieUIMaps);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Off by default. When enabled, eligible autoTrigger markers can open map links through Pie UI. Automatic copying still follows Marker clipboard settings. When off, use Open in Map manually.");
        if(ImGui::Button("Reset Info and Copy positions")) {settings.infoHud={};settings.copyHud={};changed=true;}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Returns Info and Copy HUDs to their default screen positions.");
        if(ImGui::Button("Reset activated marker state"))AttributeRuntime::ResetBehavior();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Clears Taquito's behavior timers and session activations, including permanent markers. Category checkboxes are preserved.");
        ImGui::Spacing();
    }
    if(ImGui::CollapsingHeader("Menu and Appearance")) {
        int nesting=static_cast<int>(settings.nestingStyle);
        if(ImGui::Combo("Nesting style",&nesting,"Taquito\0Taco\0\0")) {
            settings.nestingStyle=static_cast<AppSettings::NestingStyle>(nesting);changed=true;
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip(settings.nestingStyle==AppSettings::NestingStyle::Taco ?
            "Hover over an arrow to open its child menu." : "Expand categories inside the same menu.");
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Fade overlays around player",&settings.playerBubble);
        if(settings.playerBubble) {
            changed |= ImGui::SliderFloat("Clear area radius",&settings.bubbleRadius,1.0f,8.0f,"%.2f m");
            changed |= ImGui::SliderFloat("Soft fade width",&settings.bubbleFeather,0.25f,3.0f,"%.2f m");
            ImGui::TextDisabled("Affects in-world markers and trails only.");
        }
        ImGui::Spacing();
    }
    if(ImGui::CollapsingHeader("Marker Packs")) {
        if(ImGui::Button("Download marker packs...")) PackBrowserWindow::Open();
        ImGui::SameLine();
        if(ImGui::Button("Reload packs")) reload();
        if(ImGui::Button("Open packs folder")) ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        ImGui::TextWrapped("Add .taco or .zip packs here: %s",folder.string().c_str());
        if(loading) ImGui::TextDisabled("Reading packs...");
        ImGui::Spacing();
    }
    if(changed) save();
    ImGui::Separator();
    if(resetDiagnostics) {
        ImGui::SetNextItemOpen(false,ImGuiCond_Always);
        resetDiagnostics=false;
    }
    if(ImGui::CollapsingHeader("Diagnostics##TaquitoDiagnostics")) {
        const auto checkpoints=CheckpointRuntime::GetDiagnostics();
        ImGui::Text("Checkpoints: %zu nodes, %zu runs (%zu blocked, %zu finished)",checkpoints.nodes,checkpoints.runs,checkpoints.blockedRuns,checkpoints.completedRuns);
        ImGui::Text("Checkpoint contacts: %llu; resets: %llu",static_cast<unsigned long long>(checkpoints.contacts),static_cast<unsigned long long>(checkpoints.resets));
        const auto behaviorStatus=AttributeRuntime::Status();
        ImGui::TextWrapped("%s",behaviorStatus.c_str());
        const auto bridge=PathingState::GetDiagnostics();
        ImGui::TextUnformatted("Pie UI sharing");
        ImGui::Text("Sharing: %s; packs: %s",bridge.active ? "enabled" : "disabled",bridge.ready ? "ready" : "loading");
        ImGui::Text("Packs in message: %zu (%zu loaded)",bridge.packs,bridge.loadedPacks);
        ImGui::Text("State revision: %llu",static_cast<unsigned long long>(bridge.revision));
        ImGui::Text("Messages sent: %llu; requests received: %llu",static_cast<unsigned long long>(bridge.events),static_cast<unsigned long long>(bridge.requests));
        ImGui::TextWrapped("Pack folder: %s",AppRuntime::GetPacksDirectory().string().c_str());
        if(ImGui::Button("Copy Pie UI JSON")) {
            const auto json=PathingState::GetSnapshot();
            if(!json.empty()) ImGui::SetClipboardText(json.c_str());
        }
        ImGui::SameLine();
        if(ImGui::Button("Resend to Pie UI")) PathingState::Rebroadcast();
        ImGui::TextWrapped("Sent means Taquito raised the event; Pie UI does not send a receipt. Requests may come from any addon. Zero requests does not prove nobody is listening.");
        ImGui::Separator();
        size_t markerCount=0,trailCount=0;
        for(const auto& pack:packs) { markerCount+=pack.markers.size(); trailCount+=pack.trails.size(); }
        ImGui::Text("%zu packs, %zu markers, %zu trails%s",packs.size(),markerCount,trailCount,
                    loading ? " (reading packs...)" : "");
        ImGui::Text("Overlay device: %s",renderer.IsReady() ? "ready" : "unavailable");
        if(auto* mumble=AppRuntime::GetMumble(); mumble && mumble->UITick) {
            const auto map=mumble->Context.MapID;
            size_t activeMarkers=0,activeTrails=0;
            for(const auto& pack:packs) {
                for(const auto& marker:pack.markers) if(marker.map==map && TacoPack::IsVisible(marker.category) && marker.style.inWorld) ++activeMarkers;
                for(const auto& trail:pack.trails) if(trail.map==map && TacoPack::IsVisible(trail.category) && trail.style.inWorld) ++activeTrails;
            }
            ImGui::Text("Current map ID: %u (MumbleLink)",map);
            ImGui::Text("In-world on this map: %zu markers, %zu trails",activeMarkers,activeTrails);
            ImGui::Text("In-world trail segments on screen: %zu",renderer.WorldTrailSegments());
            const auto* identity=AppRuntime::GetMumbleIdentity();
            if(identity && std::isfinite(identity->FOV) && identity->FOV>.1f && identity->FOV<3.0f)
                ImGui::Text("Camera FOV: %.3f radians (live)",identity->FOV);
            else ImGui::TextUnformatted("Camera FOV: fallback until live value arrives");
        } else ImGui::TextUnformatted("Waiting for live MumbleLink game data.");
        const auto updateStatus=OnlinePacks::GetUpdateStatus();
        if(!updateStatus.empty()) ImGui::TextWrapped("Pack updates: %s",updateStatus.c_str());
        const auto online=OnlinePacks::GetSnapshot();
        if(!online.status.empty()) ImGui::TextWrapped("Pack catalog: %s",online.status.c_str());
        for(const auto& entry:online.entries) if(!entry.downloadStatus.empty())
            ImGui::TextWrapped("%s: %s",entry.name.c_str(),entry.downloadStatus.c_str());
        for(const auto& pack:packs) if(pack.containsLua) {
            ImGui::TextWrapped("%s: contains Lua (%zu script file(s)). Scripted features are unavailable; ordinary markers, trails and categories remain enabled.",
                pack.name.c_str(),pack.luaFiles);
        }
        for(const auto& pack:packs) if(!pack.diagnostics.empty()) {
            ImGui::Text("%s: %zu pack warning(s)",pack.name.c_str(),pack.diagnostics.size());
            if(ImGui::TreeNode(pack.path.string().c_str())) {
                for(const auto& diagnostic:pack.diagnostics) ImGui::TextWrapped("%s",diagnostic.c_str());
                ImGui::TreePop();
            }
        }
    }
    ImGui::End();
}
