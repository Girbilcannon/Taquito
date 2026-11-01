#include "ShortcutSettings.h"
#include "../imgui/imgui.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace {int capturing=-1;std::string warning;
std::array<bool,256> previousKeys{};std::array<bool,3> previousMouse{};unsigned releasedMouse=0,pressedMouse=0;

bool Modifier(int key){return key==16||key==17||key==18||key==91||key==92||key>=160&&key<=165;}
bool Equal(const AppSettings::Shortcut&a,const AppSettings::Shortcut&b){return a.key==b.key&&a.ctrl==b.ctrl&&a.alt==b.alt&&a.shift==b.shift;}
}
std::string ShortcutSettings::Label(const AppSettings::Shortcut&key){
    if(!key.key)return "Not set";std::string text;if(key.ctrl)text+="Ctrl+";if(key.alt)text+="Alt+";if(key.shift)text+="Shift+";
    if(key.key>=48&&key.key<=90)text+=static_cast<char>(key.key);
    else if(key.key>=112&&key.key<=135)text+="F"+std::to_string(key.key-111);
    else{switch(key.key){case 32:text+="Space";break;case 9:text+="Tab";break;case 13:text+="Enter";break;case 37:text+="Left";break;case 38:text+="Up";break;case 39:text+="Right";break;case 40:text+="Down";break;case 33:text+="Page Up";break;case 34:text+="Page Down";break;case 35:text+="End";break;case 36:text+="Home";break;case 45:text+="Insert";break;case 46:text+="Delete";break;default:text+="Key "+std::to_string(key.key);}}
    return text;
}
void ShortcutSettings::Reset(){capturing=-1;warning.clear();}
unsigned ShortcutSettings::MouseReleased(){return releasedMouse;}
unsigned ShortcutSettings::MousePressed(){return pressedMouse;}
ShortcutSettings::Result ShortcutSettings::Poll(bool gameFocused,bool allowActions){
    Result result;auto&io=ImGui::GetIO();auto&bindings=AppSettings::Get().shortcuts;
    std::array<bool,256> pressed{};bool ctrl=io.KeyCtrl,alt=io.KeyAlt,shift=io.KeyShift,super=io.KeySuper;
#if defined(_WIN32) || defined(TAQUITO_TEST_NATIVE_INPUT)
    // Nexus does not guarantee that game keyboard/mouse events reach ImGui's IO arrays.
    for(int key=8;key<256;++key){const bool down=(GetAsyncKeyState(key)&0x8000)!=0;pressed[key]=down&&!previousKeys[key];previousKeys[key]=down;}
    ctrl=previousKeys[17];alt=previousKeys[18];shift=previousKeys[16];super=previousKeys[91]||previousKeys[92];
    const int mouseKeys[]={1,2,4};releasedMouse=pressedMouse=0;
    for(int i=0;i<3;++i){const bool down=(GetAsyncKeyState(mouseKeys[i])&0x8000)!=0;if(previousMouse[i]&&!down)releasedMouse|=1u<<i;if(!previousMouse[i]&&down)pressedMouse|=1u<<i;previousMouse[i]=down;}
#else
    for(int key=8;key<256;++key)pressed[key]=ImGui::IsKeyPressed(key,false);
    releasedMouse=pressedMouse=0;for(int i=0;i<3;++i){if(ImGui::IsMouseReleased(i))releasedMouse|=1u<<i;if(ImGui::IsMouseClicked(i))pressedMouse|=1u<<i;}
#endif
    if(!gameFocused){releasedMouse=pressedMouse=0;return result;}
    if(capturing>=0){
        for(int key=8;key<256;++key)if(!Modifier(key)&&pressed[key]){
            if(key==27){Reset();return result;}
            if(key==8){bindings[capturing]={};Reset();result.changed=true;return result;}
            if(super)return result;
            AppSettings::Shortcut candidate{key,ctrl,alt,shift};
            for(int i=0;i<static_cast<int>(bindings.size());++i)if(i!=capturing&&Equal(candidate,bindings[i])){warning="That shortcut is already assigned to another Taquito action.";return result;}
            bindings[capturing]=candidate;Reset();result.changed=true;return result;
        }return result;
    }
    if(!allowActions||io.WantTextInput||super)return result;
    for(unsigned i=0;i<bindings.size();++i){const auto&key=bindings[i];if(key.key>0&&key.key<256&&key.ctrl==ctrl&&key.alt==alt&&key.shift==shift&&pressed[key.key])result.actions|=1u<<i;}
    return result;
}
void ShortcutSettings::Draw(){
    static const char*names[]={"Disable Taquito Rendering","Next Preset","Previous Preset","Open/Close Quick Menu"};
    auto&keys=AppSettings::Get().shortcuts;
    bool captureHovered=false;
    for(int i=0;i<static_cast<int>(keys.size());++i){ImGui::PushID(i);ImGui::TextUnformatted(names[i]);ImGui::SameLine(290);auto label=capturing==i?std::string("Press a key..."):Label(keys[i]);if(ImGui::Button(label.c_str(),{165,0})){capturing=i;warning.clear();}if(capturing==i&&ImGui::IsItemHovered())captureHovered=true;ImGui::PopID();}
    if(capturing>=0&&ImGui::IsMouseClicked(0)&&!captureHovered)Reset();
    if(!warning.empty())ImGui::TextWrapped("%s",warning.c_str());
}
