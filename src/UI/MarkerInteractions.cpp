#include "MarkerInteractions.h"
#include "../Core/AppSettings.h"
#include "../Core/MarkerActions.h"
#include "../imgui/imgui.h"
#include <atomic>
#include <cstring>
#include <unordered_set>

namespace {
MarkerActions::Proximity proximity;
std::unordered_set<const TacoPack::Marker*> dismissed;
std::atomic_bool requested{false};
std::string notification;
double notificationUntil=0,lastAutomatic=-10;
bool Copy(const std::string& text,const std::string& message) {
    if(text.empty() || AppSettings::Get().clipboardMode==AppSettings::ClipboardMode::Never) return false;
    ImGui::SetClipboardText(text.c_str());
    const auto* clipboard=ImGui::GetClipboardText();
    const bool success=clipboard && std::strcmp(clipboard,text.c_str())==0;
    notification=success ? MarkerActions::Message(text,message) : "Clipboard unavailable. Try Copy again.";
    notificationUntil=ImGui::GetTime()+3;
    return success;
}
void DrawInfoHud(const TacoPack::Marker* info,ImVec2 display) {
    const float scale=ImGui::GetFontSize()/13.0f;
    const ImVec2 padding{12*scale,10*scale};
    const float width=std::min(440*scale,std::max(160.0f,display.x-24));
    const float closeSize=20*scale,headerHeight=closeSize+padding.y*2;
    const float wrap=std::max(80.0f,width-padding.x*2-ImGui::GetStyle().ScrollbarSize);
    const float contentHeight=ImGui::CalcTextSize(info->style.info.c_str(),nullptr,false,wrap).y;
    const float available=std::max(40.0f,display.y-30-headerHeight-padding.y*2-12);
    const float bodyHeight=std::min(std::max(ImGui::GetFontSize()+4,contentHeight+8*scale),std::min(320*scale,available));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,8*scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,padding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,4*scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{6*scale,4*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,8*scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,{.12f,.13f,.15f,.85f});
    ImGui::PushStyleColor(ImGuiCol_Border,{.55f,.57f,.61f,.28f});
    ImGui::PushStyleColor(ImGuiCol_Text,{.94f,.94f,.95f,1});
    ImGui::PushStyleColor(ImGuiCol_Button,{.28f,.29f,.32f,.95f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.34f,.36f,.40f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.18f,.43f,.71f,1});
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,{0,0,0,.12f});
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,{.42f,.44f,.48f,.8f});
    ImGui::SetNextWindowPos({display.x*.5f,30},ImGuiCond_Always,{.5f,0});
    ImGui::SetNextWindowSize({width,headerHeight+padding.y*2+bodyHeight},ImGuiCond_Always);
    if(ImGui::Begin("Info##TaquitoMarkerInfo",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto pos=ImGui::GetWindowPos();auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos,{pos.x+width,pos.y+headerHeight},IM_COL32(20,22,26,245),8*scale,ImDrawCornerFlags_Top);
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Info");
        ImGui::SameLine();ImGui::SetCursorPos({width-padding.x-closeSize,padding.y});
        const auto button=ImGui::GetCursorScreenPos();
        const bool closed=ImGui::InvisibleButton("##CloseInfo",{closeSize,closeSize});
        const bool hovered=ImGui::IsItemHovered();
        if(hovered) {
            draw->AddRectFilled(button,{button.x+closeSize,button.y+closeSize},IM_COL32(100,105,115,120),4*scale);
            ImGui::SetTooltip("Dismiss until you leave this marker's range");
        }
        const ImVec2 center{button.x+closeSize*.5f,button.y+closeSize*.5f};
        const float arm=4*scale;
        draw->AddLine({center.x-arm,center.y-arm},{center.x+arm,center.y+arm},IM_COL32(235,235,240,255),1.5f*scale);
        draw->AddLine({center.x-arm,center.y+arm},{center.x+arm,center.y-arm},IM_COL32(235,235,240,255),1.5f*scale);
        if(closed) {
            if(info) dismissed.insert(info);
        }
        ImGui::SetCursorPos({padding.x,headerHeight+padding.y});
        if(ImGui::BeginChild("##InfoBody",{0,bodyHeight},false,ImGuiWindowFlags_NoSavedSettings)) {
            if(info) ImGui::TextWrapped("%s",info->style.info.c_str());

        }
        ImGui::EndChild();
    }
    ImGui::End();ImGui::PopStyleColor(8);ImGui::PopStyleVar(6);
}
}
void MarkerInteractions::RequestInteract() {requested.store(true,std::memory_order_release);}
void MarkerInteractions::Reset() {
    proximity.Reset();requested.store(false,std::memory_order_release);
    dismissed.clear();
    notification.clear();notificationUntil=0;lastAutomatic=-10;
}
void MarkerInteractions::CopyCategory(const std::string& text,const std::string& message) {Copy(text,message);}
void MarkerInteractions::Render(const std::vector<TacoPack::Pack>& packs,const Mumble::Data* mumble,bool enabled) {
    const bool interact=requested.exchange(false,std::memory_order_acq_rel);
    const bool active=enabled && mumble && mumble->UITick && mumble->Context.MapID &&
        mumble->Context.IsGameFocused && !mumble->Context.IsMapOpen;
    const TacoPack::Vec3 player=mumble ? TacoPack::Vec3{mumble->AvatarPosition.X,mumble->AvatarPosition.Y,mumble->AvatarPosition.Z} : TacoPack::Vec3{};
    const auto selection=proximity.Update(packs,mumble ? mumble->Context.MapID : 0,player,active);
    if(active) for(auto it=dismissed.begin();it!=dismissed.end();) {
        if(!proximity.IsInfoInside(*it)) it=dismissed.erase(it);else ++it;
    }
    const auto& settings=AppSettings::Get();
    const bool mayCopy=active && !mumble->Context.IsTextboxFocused &&
        settings.clipboardMode!=AppSettings::ClipboardMode::Never && !ImGui::GetIO().WantTextInput;
    if(mayCopy && interact && selection.copy) Copy(selection.copy->style.copy,selection.copy->style.copyMessage);
    else if(mayCopy && selection.automatic && settings.clipboardMode==AppSettings::ClipboardMode::Always &&
            ImGui::GetTime()-lastAutomatic>=2) {
        if(Copy(selection.automatic->style.copy,selection.automatic->style.copyMessage)) lastAutomatic=ImGui::GetTime();
    }
    const auto* info=active && settings.showMarkerInfo && !dismissed.count(selection.info) ? selection.info : nullptr;
    const auto* copy=mayCopy ? selection.copy : nullptr;
    const auto display=ImGui::GetIO().DisplaySize;
    if(info && display.x>0 && display.y>0) DrawInfoHud(info,display);
    if(copy && display.x>0 && display.y>0) {
        ImGui::SetNextWindowPos({display.x*.5f,display.y*.72f},ImGuiCond_FirstUseEver,{.5f,0});
        ImGui::SetNextWindowSize({std::min(480.0f,display.x*.8f),0},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({150,40},{std::max(150.0f,std::min(600.0f,display.x*.9f)),std::max(40.0f,display.y*.45f)});
        if(ImGui::Begin("Copy##TaquitoMarkerCopy",nullptr,ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) {
            if(ImGui::Button("Copy text")) Copy(copy->style.copy,copy->style.copyMessage);
            ImGui::TextWrapped("%s",copy->style.copy.c_str());
            ImGui::TextWrapped("Marker interaction keybind: default Alt+F");
        }
        ImGui::End();
    }
    // Separator clicks can also show feedback when world overlays are off.
    if(mumble && mumble->Context.IsGameFocused && ImGui::GetTime()<notificationUntil && !notification.empty()) {
        ImGui::SetNextWindowPos({display.x*.5f,display.y*.18f},ImGuiCond_Always,{.5f,0});
        ImGui::SetNextWindowSizeConstraints({0,0},{std::max(100.0f,display.x*.8f),std::max(40.0f,display.y*.25f)});
        if(ImGui::Begin("##TaquitoCopyNotice",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*32);
            ImGui::TextUnformatted(notification.c_str());ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }
}
