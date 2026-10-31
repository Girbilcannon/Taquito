#include "PresetsWindow.h"
#include "../Core/Presets.h"
#include "../Core/AppSettings.h"
#include "../imgui/imgui.h"
namespace {bool shown=false;std::string renameId;char nameBuffer[256]{};}
void PresetsWindow::Open(){shown=true;}
void PresetsWindow::Close(){shown=false;renameId.clear();}
void PresetsWindow::Render(std::vector<TacoPack::Pack>&packs,void(*changed)()){
    if(!shown||!Presets::Enabled())return;
    ImGui::SetNextWindowSize({330,260},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Presets##Taquito",&shown,ImGuiWindowFlags_NoSavedSettings)){
        if(!AppSettings::Get().renderingEnabled)ImGui::TextColored({1,.25f,.25f,1},"Rendering Disabled");
        bool dirty=false;std::string choose,duplicate,remove,moveId,moveTarget;bool moveAfter=false;
        if(ImGui::BeginChild("##PresetList",{0,-ImGui::GetFrameHeightWithSpacing()*2},true)){
            for(const auto&p:Presets::Items()){
                ImGui::PushID(p.id.c_str());if(ImGui::Selectable(p.name.c_str(),p.id==Presets::Selected()))choose=p.id;
                const auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();
                if(ImGui::BeginDragDropSource()){
                    ImGui::SetDragDropPayload("TAQUITO_PRESET",p.id.c_str(),p.id.size()+1);
                    ImGui::TextUnformatted(p.name.c_str());ImGui::EndDragDropSource();
                }
                if(ImGui::BeginDragDropTarget()){
                    if(const auto*payload=ImGui::AcceptDragDropPayload("TAQUITO_PRESET",ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)){
                        const bool after=ImGui::GetMousePos().y>(rowMin.y+rowMax.y)*.5f;
                        const float y=after?rowMax.y:rowMin.y;
                        ImGui::GetWindowDrawList()->AddLine({rowMin.x,y},{rowMax.x,y},ImGui::GetColorU32(ImGuiCol_HeaderActive),2.0f);
                        if(payload->IsDelivery()){moveId=static_cast<const char*>(payload->Data);moveTarget=p.id;moveAfter=after;}
                    }ImGui::EndDragDropTarget();
                }
                if(ImGui::BeginPopupContextItem("##PresetActions")){
                    if(ImGui::MenuItem("Rename")){
                        renameId=p.id;
                        const auto copied=p.name.copy(nameBuffer,sizeof(nameBuffer)-1);
                        nameBuffer[copied]='\0';
                    }
                    if(ImGui::MenuItem("Duplicate"))duplicate=p.id;
                    if(ImGui::MenuItem("Remove",nullptr,false,Presets::Items().size()>1))remove=p.id;
                    ImGui::EndPopup();
                }ImGui::PopID();
            }
        }ImGui::EndChild();
        if(!choose.empty())dirty|=Presets::Select(choose,packs);
        if(!duplicate.empty())dirty|=Presets::Duplicate(duplicate,packs);
        if(!remove.empty())dirty|=Presets::Remove(remove,packs);
        if(!moveId.empty())dirty|=Presets::Move(moveId,moveTarget,moveAfter);
        if(ImGui::Button("+",{35,0}))dirty|=Presets::Add(packs);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Add a preset with top-level packs off and children on (up to 128 presets).");
        ImGui::SameLine();if(ImGui::Button("-",{35,0}))dirty|=Presets::Remove(Presets::Selected(),packs);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Remove the selected preset. At least one preset is kept.");
        if(!renameId.empty()){
            ImGui::OpenPopup("Rename preset");
            if(ImGui::BeginPopupModal("Rename preset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                const bool enter=ImGui::InputText("Name",nameBuffer,sizeof(nameBuffer),ImGuiInputTextFlags_EnterReturnsTrue);
                if(ImGui::Button("Save")||enter){if(Presets::Rename(renameId,nameBuffer)){dirty=true;renameId.clear();ImGui::CloseCurrentPopup();}}
                ImGui::SameLine();if(ImGui::Button("Cancel")){renameId.clear();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
            }
        }
        if(dirty&&changed)changed();
        if(!Presets::Status().empty())ImGui::TextWrapped("%s",Presets::Status().c_str());
    }ImGui::End();
}
