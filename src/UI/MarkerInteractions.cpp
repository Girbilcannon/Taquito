#include "MarkerInteractions.h"
#include "../Core/AppSettings.h"
#include "../Core/MarkerActions.h"
#include "../Core/AttributeRuntime.h"
#include "../Core/CheckpointRuntime.h"
#include "../Core/PieUIBridge.h"
#include "HudPlacement.h"
#include <algorithm>
#include <cmath>
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
bool hudChanged=false;
bool Copy(const std::string& text,const std::string& message) {
    if(text.empty() || AppSettings::Get().clipboardMode==AppSettings::ClipboardMode::Never) return false;
    ImGui::SetClipboardText(text.c_str());
    const auto* clipboard=ImGui::GetClipboardText();
    const bool success=clipboard && std::strcmp(clipboard,text.c_str())==0;
    notification=success ? MarkerActions::Message(text,message) : "Clipboard unavailable. Try Copy again.";
    notificationUntil=ImGui::GetTime()+3;
    return success;
}
void RememberHud(AppSettings::HudPosition& saved,ImVec2 display) {
    auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    auto bounds=HudPlacement::Rescue(pos.x,pos.y,size.x,size.y,display.x,display.y);
    if(bounds.width!=size.x||bounds.height!=size.y)ImGui::SetWindowSize({bounds.width,bounds.height});
    if(bounds.x!=pos.x||bounds.y!=pos.y)ImGui::SetWindowPos({bounds.x,bounds.y});
    if(!saved.saved||std::abs(saved.x-bounds.x)>.1f||std::abs(saved.y-bounds.y)>.1f) {
        saved={true,bounds.x,bounds.y};hudChanged=true;
    }
}
void DrawInfoHud(const TacoPack::Marker* info,ImVec2 display) {
    const float scale=ImGui::GetFontSize()/13.0f;
    const ImVec2 padding{12*scale,10*scale};
    const float width=std::min(440*scale,std::max(1.0f,display.x-16));
    const float closeSize=20*scale,headerHeight=closeSize+padding.y*2;
    const float wrap=std::max(80.0f,width-padding.x*2-ImGui::GetStyle().ScrollbarSize);
    const float contentHeight=ImGui::CalcTextSize(info->style.info.c_str(),nullptr,false,wrap).y;
    const float available=std::max(40.0f,display.y-30-headerHeight-padding.y*2-12);
    const float bodyHeight=std::min(std::max(ImGui::GetFontSize()+4,contentHeight+8*scale),std::min(320*scale,available));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,{1,1});
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
    auto& saved=AppSettings::Get().infoHud;
    const auto bounds=HudPlacement::Rescue(saved.saved ? saved.x : (display.x-width)*.5f,
        saved.saved ? saved.y : 30,width,headerHeight+padding.y*2+bodyHeight,display.x,display.y);
    ImGui::SetNextWindowPos({bounds.x,bounds.y},ImGuiCond_Always);
    ImGui::SetNextWindowSize({bounds.width,bounds.height},ImGuiCond_Always);
    if(ImGui::Begin("Info##TaquitoMarkerInfo",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto pos=ImGui::GetWindowPos();auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos,{pos.x+width,pos.y+headerHeight},IM_COL32(20,22,26,245),8*scale,ImDrawCornerFlags_Top);
        // Custom title strip is the drag handle; leave the close button independent.
        ImGui::SetCursorPos({padding.x,padding.y});
        ImGui::InvisibleButton("##DragInfo",{std::max(1.0f,width-padding.x*2-closeSize-8*scale),closeSize});
        if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,0)) {
            auto delta=ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos({pos.x+delta.x,pos.y+delta.y});
        }
        draw->AddText({pos.x+padding.x,pos.y+padding.y+4*scale},IM_COL32(240,240,242,255),"Info");
        ImGui::SetCursorPos({width-padding.x-closeSize,padding.y});
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
    RememberHud(saved,display);
    ImGui::End();ImGui::PopStyleColor(8);ImGui::PopStyleVar(7);
}
}
void MarkerInteractions::RequestInteract() {requested.store(true,std::memory_order_release);}
void MarkerInteractions::Reset() {
    proximity.Reset();requested.store(false,std::memory_order_release);
    dismissed.clear();
    AttributeRuntime::ResetFocus();
    notification.clear();notificationUntil=0;lastAutomatic=-10;
}
void MarkerInteractions::CopyCategory(const std::string& text,const std::string& message) {Copy(text,message);}
void MarkerInteractions::Render(const std::vector<TacoPack::Pack>& packs,const Mumble::Data* mumble,bool enabled,void (*save)(),void (*categoryChanged)()) {
    PieUIBridge::Poll(ImGui::GetTime());
    const bool interact=requested.exchange(false,std::memory_order_acq_rel);
    const bool active=enabled && mumble && mumble->UITick && mumble->Context.MapID &&
        mumble->Context.IsGameFocused && !mumble->Context.IsMapOpen;
    const TacoPack::Vec3 player=mumble ? TacoPack::Vec3{mumble->AvatarPosition.X,mumble->AvatarPosition.Y,mumble->AvatarPosition.Z} : TacoPack::Vec3{};
    const auto selection=proximity.Update(packs,mumble ? mumble->Context.MapID : 0,player,active,static_cast<bool (*)(const TacoPack::Marker&)>(AttributeRuntime::Visible));
    if(active) for(auto it=dismissed.begin();it!=dismissed.end();) {
        if(!proximity.IsInfoInside(*it)) it=dismissed.erase(it);else ++it;
    }
    const auto& settings=AppSettings::Get();
    const bool mayCopy=active && !mumble->Context.IsTextboxFocused &&
        settings.clipboardMode!=AppSettings::ClipboardMode::Never && !ImGui::GetIO().WantTextInput;
    const bool mayActivate=active && !mumble->Context.IsTextboxFocused && !ImGui::GetIO().WantTextInput;
    bool categoriesChanged=false;
    const auto activate=[&](const TacoPack::Marker* marker,bool automatic) {
        if(!marker || !AttributeRuntime::Visible(*marker))return;
        // Category actions and behavior activation are independent of clipboard policy.
        if(mayCopy && !marker->style.copy.empty() && (!automatic ||
            (settings.clipboardMode==AppSettings::ClipboardMode::Always && ImGui::GetTime()-lastAutomatic>=2))) {
            const bool copied=Copy(marker->style.copy,marker->style.copyMessage);
            // Manual Copy remains copy-only. Only an eligible automatic entry also opens a map link.
            const bool opened=automatic&&settings.autoTriggerPieUIMaps&&PieUIBridge::OpenMap(marker->style.copy);
            if(automatic&&(copied||opened))lastAutomatic=ImGui::GetTime();
        }
        AttributeRuntime::BeginActivationBatch();
        categoriesChanged |= AttributeRuntime::Activate(*marker,packs);
        const bool reset=CheckpointRuntime::ResetFrom(*marker);
        AttributeRuntime::EndActivationBatch();
        if(reset || AttributeRuntime::HasLoadedResetTarget(*marker,packs)) {
            notification=CheckpointAttributes::Trim(marker->style.resetMessage).empty()?std::string{}:marker->style.resetMessage;
            notificationUntil=notification.empty()?0:ImGui::GetTime()+3.5;
        }
    };
    AttributeRuntime::BeginActivationBatch();
    if(mayActivate && interact) activate(selection.action,false);
    if(mayActivate) for(const auto* marker:selection.entered)
        if(!interact || marker!=selection.action)activate(marker,true);
    AttributeRuntime::EndActivationBatch();
    const auto* info=active && settings.showMarkerInfo && selection.info && AttributeRuntime::Visible(*selection.info) && !dismissed.count(selection.info) ? selection.info : nullptr;
    const auto* copy=mayCopy && selection.copy && AttributeRuntime::Visible(*selection.copy) ? selection.copy : nullptr;
    const auto display=ImGui::GetIO().DisplaySize;
    if(info && display.x>0 && display.y>0) DrawInfoHud(info,display);
    if(copy && display.x>0 && display.y>0) {
        auto& saved=AppSettings::Get().copyHud;
        const float width=std::min(480.0f,std::max(1.0f,display.x-16));
        ImGui::SetNextWindowPos(saved.saved ? ImVec2{saved.x,saved.y} : ImVec2{(display.x-width)*.5f,display.y*.72f},saved.saved ? ImGuiCond_Appearing : ImGuiCond_Always);
        ImGui::SetNextWindowSize({width,0},ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints({std::min(150.0f,width),std::min(40.0f,display.y)},
            {width,std::max(std::min(40.0f,display.y),display.y*.45f)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,{1,1});
        if(ImGui::Begin("Copy##TaquitoMarkerCopy",nullptr,ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) {
            if(ImGui::Button("Copy text")) activate(copy,false);
            if(PieUIBridge::CanOpenMap(copy->style.copy)) {
                const float needed=ImGui::CalcTextSize("Open in Map").x+ImGui::GetStyle().FramePadding.x*2;
                const float remaining=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x-ImGui::GetItemRectMax().x-ImGui::GetStyle().ItemSpacing.x;
                if(remaining>=needed)ImGui::SameLine();
                if(ImGui::Button("Open in Map"))PieUIBridge::OpenMap(copy->style.copy);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Open this waypoint or point of interest through Pie UI");
            }
            ImGui::TextWrapped("%s",copy->style.copy.c_str());
            ImGui::TextWrapped("Marker interaction keybind: default Alt+F");
        }
        RememberHud(saved,display);
        ImGui::End();ImGui::PopStyleVar();
    }
    if(categoriesChanged) {if(categoryChanged)categoryChanged();else if(save)save();}
    if(hudChanged && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && save) {save();hudChanged=false;}
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
