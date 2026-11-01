#include "PackBrowserWindow.h"
#include "../Core/AppRuntime.h"
#include "../Core/OnlinePacks.h"
#include "../imgui/imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace {
bool shown=false;
std::string selected;
char search[128]{};
void OpenWebPage(const std::string& url) {
    if(!url.starts_with("https://")) return;
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),nullptr,0);
    if(n<=0) return;
    std::wstring wide(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),wide.data(),n);
    ShellExecuteW(nullptr,L"open",wide.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
bool Matches(const std::string& value, const std::string& term) {
    std::string lower=value;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return lower.find(term)!=std::string::npos;
}
void Title(const char* text) {
    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
}
}
void PackBrowserWindow::Open() {
    shown=true;
    OnlinePacks::Start(AppRuntime::GetPacksDirectory());
}
void PackBrowserWindow::Close() { shown=false; selected.clear(); }
void PackBrowserWindow::Render() {
    if(!shown) return;
    ImGui::SetNextWindowSize({820,540},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Download Marker Packs##Taquito",&shown)) {
        const auto snapshot=OnlinePacks::GetSnapshot();
        Title("Marker packs");
        ImGui::SameLine();
        ImGui::TextDisabled("%s",snapshot.status.c_str());
        ImGui::Separator();
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("Search",search,sizeof(search));
        ImGui::SameLine();
        if(ImGui::Button("Refresh list")) OnlinePacks::Refresh();
        ImGui::Spacing();
        // The two panes leave space below for a permanent source note.
        const float footer=ImGui::GetTextLineHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("Pack list",{290,-footer},true);
        std::string term=search;
        std::transform(term.begin(),term.end(),term.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        for(const auto& entry:snapshot.entries) {
            if(!term.empty() && !Matches(entry.name,term) && !Matches(entry.author,term) && !Matches(entry.categories,term)) continue;
            ImGui::PushID(entry.id.c_str());
            if(ImGui::Selectable(entry.name.c_str(),selected==entry.id)) {
                selected=entry.id;
                OnlinePacks::Describe(entry.id);
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",entry.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Pack details",{0,-footer},true);
        const OnlinePacks::Entry* current=nullptr;
        for(const auto& entry:snapshot.entries) if(entry.id==selected) { current=&entry; break; }
        if(current) {
            ImGui::Indent(8);
            ImGui::Spacing();
            Title(current->name.c_str());
            if(!current->author.empty()) ImGui::TextDisabled("By %s",current->author.c_str());
            if(!current->version.empty()) ImGui::TextDisabled("Latest version: %s",current->version.c_str());
            if(!current->installedVersion.empty()) ImGui::TextDisabled("Installed version: %s",current->installedVersion.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if(current->descriptionPending) ImGui::TextDisabled("Loading description...");
            else if(current->descriptionReady) ImGui::TextWrapped("%s",current->description.c_str());
            else ImGui::TextDisabled("No description available.");
            if(!current->categories.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Categories");
                ImGui::TextWrapped("%s",current->categories.c_str());
            }
            ImGui::Spacing();
            const auto destination=AppRuntime::GetPacksDirectory()/current->filename;
            std::error_code ec;
            const bool installed=std::filesystem::exists(destination,ec);
            ImGui::TextDisabled("%s",current->filename.c_str());
            if(installed) ImGui::TextDisabled("Installed in TacoPacks");
            if(!current->downloadStatus.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s",current->downloadStatus.c_str());
            }
            ImGui::Unindent(8);
            ImGui::Separator();
            if(current->downloading) ImGui::TextDisabled("Downloading...");
            else {
                const ImVec4 primary=ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
                ImGui::PushStyleColor(ImGuiCol_Button,primary);
                if(ImGui::Button(installed ? "Download / replace" : "Download pack",{155,30})) OnlinePacks::Download(current->id);
                ImGui::PopStyleColor();
            }
            if(!current->repository.empty() || current->pageUrl.starts_with("https://")) {
                ImGui::SameLine();
                if(ImGui::Button(current->repository.empty() ? "Website" : "GitHub",{95,30}))
                    OpenWebPage(current->repository.empty() ? current->pageUrl : current->repository);
            }
            if(!current->repository.empty() && current->pageUrl.starts_with("https://") && current->pageUrl!=current->repository) {
                ImGui::SameLine();
                if(ImGui::Button("More info",{95,30})) OpenWebPage(current->pageUrl);
            }
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("Select a marker pack to see its details.");
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Downloads are linked directly from Blish HUD's marker pack catalog.");
    }
    ImGui::End();
}
