#include "CategoryMenu.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include <cstdint>

namespace {
// The main menu is a regular window. Its own child popup must not block
// sibling rows (including checkboxes and leaves) from receiving mouse hover.
struct ParentMenuHover {
    ImGuiWindow* window=ImGui::GetCurrentWindow();
    ImGuiWindow* previous=GImGui->NavWindow;
    bool active=false;
    explicit ParentMenuHover(bool taco) {
        if(!taco || !previous || !(previous->Flags & ImGuiWindowFlags_ChildMenu)) return;
        for(auto* parent=previous->ParentWindow; parent; parent=parent->ParentWindow)
            if(parent==window) { active=true; GImGui->NavWindow=window; break; }
    }
    ~ParentMenuHover() {
        if(active && GImGui->NavWindow==window)
            for(const auto& popup:GImGui->OpenPopupStack)
                if(popup.Window==previous) { GImGui->NavWindow=previous; break; }
    }
};
void Tip(const TacoPack::Category& category,bool hovered) {
    if(hovered && !category.parent && category.containsLua) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*32.0f);
        ImGui::TextUnformatted("This pack contains Lua scripts for more advanced features. Taquito does not support Lua scripts. If you need access to these features, your best option is to run Blish HUD and install the Pathing Module.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        return;
    }
    if(auto* text=TacoPack::Attribute(category.style,"tip-description"); text && !text->empty() && hovered)
        ImGui::SetTooltip("%s",text->c_str());
}
bool HasChildren(const TacoPack::Category& category) {
    for(const auto& child:category.children) if(!child->hidden) return true;
    return false;
}
std::string ContextId(const TacoPack::Category& category) {
    return "##NextTier_"+std::to_string(reinterpret_cast<std::uintptr_t>(&category));
}
void NextTierMenu(TacoPack::Category& category,bool checkboxClicked,bool taco,void (*changed)()) {
    if(category.children.empty()) return;
    const auto id=ContextId(category);
    bool clicked=checkboxClicked;
    {
        ParentMenuHover hover(taco);
        clicked |= ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    }
    if(clicked)
        ImGui::OpenPopup(id.c_str());
    if(ImGui::BeginPopup(id.c_str())) {
        if(ImGui::MenuItem("Enable All")) {
            bool modified=false;
            for(auto& child:category.children) {
                if(!child->hidden && !child->separator && !child->visible) {
                    child->visible=true; modified=true;
                }
            }
            if(modified && changed) changed();
        }
        if(ImGui::MenuItem("Disable Next Tier")) {
            bool modified=false;
            for(auto& child:category.children) {
                // Only direct child checkboxes; no recursive changes.
                if(!child->hidden && !child->separator && child->visible) {
                    child->visible=false; modified=true;
                }
            }
            if(modified && changed) changed();
        }
        ImGui::EndPopup();
    }
}
}
void CategoryMenu::Draw(TacoPack::Category& category,AppSettings::NestingStyle style,void (*changed)()) {
    if(category.hidden) return;
    if(style==AppSettings::NestingStyle::Taco) ImGui::PushID(&category);
    else ImGui::PushID(category.key.c_str());
    const bool markLua=!category.parent && category.containsLua;
    const auto displayName=category.name+(markLua ? " (Contains Lua)" : "");
    if(category.separator) {
        ImGui::Separator(); ImGui::TextDisabled("%s",displayName.c_str());
        Tip(category,ImGui::IsItemHovered());
        NextTierMenu(category,false,style==AppSettings::NestingStyle::Taco,changed); ImGui::PopID(); return;
    }
    bool visible=category.visible;
    bool checkboxContext=false;
    {
        ParentMenuHover hover(style==AppSettings::NestingStyle::Taco);
        if(ImGui::Checkbox("##toggle",&visible)) {
            category.visible=visible; if(changed) changed();
        }
        checkboxContext=ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    }
    ImGui::SameLine();
    if(style==AppSettings::NestingStyle::Taco) {
        ImGui::PopID();
        // Sibling BeginMenu calls must share the same parent ID scope for
        // ImGui to recognize them as one menu set. Keep uniqueness in labels.
        const auto label=displayName+"###category_"+
            std::to_string(reinterpret_cast<std::uintptr_t>(&category));
        // ImGui's native submenu handles hover delay, right arrows and crossing
        // into a child popup without closing it. Checkboxes keep popups open.
        if(HasChildren(category)) {
            auto* parentWindow=ImGui::GetCurrentWindow();
            const bool opened=ImGui::BeginMenu(label.c_str(),!ImGui::IsPopupOpen(ContextId(category).c_str()));
            // BeginMenu enters the child popup; test the row in its parent window.
            const bool hovered=GImGui->HoveredWindow==parentWindow &&
                parentWindow->DC.LastItemRect.Contains(ImGui::GetIO().MousePos);
            if(opened) {
                for(auto& child:category.children) Draw(*child,style,changed);
                ImGui::EndMenu();
            }
            Tip(category,hovered);
        } else {
            ParentMenuHover hover(true);
            if(ImGui::Selectable(label.c_str(),false,ImGuiSelectableFlags_DontClosePopups)) {
                category.visible=!category.visible; if(changed) changed();
            }
            Tip(category,ImGui::IsItemHovered());
        }
        NextTierMenu(category,checkboxContext,true,changed);
        return;
    } else {
        bool expanded=false;
        if(!HasChildren(category)) ImGui::TextUnformatted(displayName.c_str());
        else expanded=ImGui::TreeNodeEx((displayName+"###"+category.name).c_str(),ImGuiTreeNodeFlags_SpanAvailWidth);
        Tip(category,ImGui::IsItemHovered());
        NextTierMenu(category,checkboxContext,false,changed);
        if(expanded) {
            for(auto& child:category.children) Draw(*child,style,changed);
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}
