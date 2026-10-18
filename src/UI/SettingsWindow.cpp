#include "SettingsWindow.h"
#include "PackBrowserWindow.h"
#include "OverlayRenderer.h"
#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/OnlinePacks.h"
#include "../imgui/imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <cmath>

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
    ImGui::SetNextWindowSize({590,540},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Taquito Settings##Taquito",&shown,ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }
    const auto& folder=AppRuntime::GetPacksDirectory();
    ImGui::TextWrapped("Drop .taco and .zip marker packs into: %s",folder.string().c_str());
    if(ImGui::Button("Reload packs")) reload();
    ImGui::SameLine();
    if(ImGui::Button("Open packs folder")) ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    ImGui::SameLine();
    if(ImGui::Button("Download Marker Packs...")) PackBrowserWindow::Open();
    ImGui::Separator();
    // These switches multiply XML-defined sizes; save only when an actual control changes.
    auto& settings=AppSettings::Get();
    bool changed=false;
    int nesting=static_cast<int>(settings.nestingStyle);
    if(ImGui::Combo("Nesting Style",&nesting,"Taquito\0Taco\0\0")) {
        settings.nestingStyle=static_cast<AppSettings::NestingStyle>(nesting); changed=true;
    }
    ImGui::TextDisabled(settings.nestingStyle==AppSettings::NestingStyle::Taco ?
        "Hover over an arrow to open its child menu." : "Expand categories inside the same menu.");
    ImGui::Separator();
    changed |= ImGui::Checkbox("In-world markers and trails",&settings.world);
    changed |= ImGui::Checkbox("Minimap markers and trails",&settings.mini);
    changed |= ImGui::Checkbox("Full map markers and trails",&settings.big);
    changed |= ImGui::SliderFloat("Minimap icon scale",&settings.miniIconSize,0.25f,3.0f,"%.2fx");
    changed |= ImGui::SliderFloat("Minimap trail line scale",&settings.miniTrailSize,0.25f,3.0f,"%.2fx");
    changed |= ImGui::SliderFloat("Full map icon scale",&settings.bigIconSize,0.25f,3.0f,"%.2fx");
    changed |= ImGui::SliderFloat("Full map trail line scale",&settings.bigTrailSize,0.25f,3.0f,"%.2fx");
    changed |= ImGui::SliderFloat("In-world marker scale",&settings.worldIconSize,0.25f,3.0f,"%.2fx");
    changed |= ImGui::SliderFloat("In-world trail width",&settings.worldTrailSize,0.25f,3.0f,"%.2fx");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Show marker information",&settings.showMarkerInfo);
    int clipboard=static_cast<int>(settings.clipboardMode);
    if(ImGui::Combo("Marker clipboard",&clipboard,"Always (including auto-trigger)\0Only when interacted with\0Never\0\0")) {
        settings.clipboardMode=static_cast<AppSettings::ClipboardMode>(clipboard);changed=true;
    }
    ImGui::TextWrapped("Nearby marker copy: click Copy text or use the Taquito marker interaction keybind (default Alt+F). Change the keybind in Nexus settings.");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Fade overlays around player",&settings.playerBubble);
    if(settings.playerBubble) {
        changed |= ImGui::SliderFloat("Clear area radius",&settings.bubbleRadius,1.0f,8.0f,"%.2f m");
        changed |= ImGui::SliderFloat("Soft fade width",&settings.bubbleFeather,0.25f,3.0f,"%.2f m");
        ImGui::TextDisabled("Affects in-world markers and trails only.");
    }
    if(changed) save();
    ImGui::Separator();
    if(resetDiagnostics) {
        ImGui::SetNextItemOpen(false,ImGuiCond_Always);
        resetDiagnostics=false;
    }
    if(ImGui::CollapsingHeader("Diagnostics##TaquitoDiagnostics")) {
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
