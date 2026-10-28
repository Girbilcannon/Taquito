#include "PieUIBridge.h"
#include <atomic>
#include <cmath>
#include <cstdint>

namespace {
std::atomic<PieUIBridge::RaiseEvent> sender{nullptr};
std::atomic_bool present{false};
double lastProbe=-1;
int Base64(char c) {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
}
void PieUIBridge::Initialize(RaiseEvent raise){sender.store(raise);present.store(false);lastProbe=-1;}
void PieUIBridge::Shutdown(){sender.store(nullptr);present.store(false);lastProbe=-1;}
void PieUIBridge::OnReady(void*){if(sender.load())present.store(true);}
void PieUIBridge::Probe(){
    present.store(false);
    if(auto send=sender.load())send(PingEvent,nullptr);
}
void PieUIBridge::Poll(double now){
    if(!std::isfinite(now))return;
    if(lastProbe<0||now<lastProbe||now-lastProbe>=1){lastProbe=now;Probe();}
}
bool PieUIBridge::Present(){return sender.load()!=nullptr&&present.load();}
std::string PieUIBridge::MapLink(const std::string&text){
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    if(first==std::string::npos||last-first+1!=11)return {};
    const auto link=text.substr(first,11);
    if(link[0]!='['||link[1]!='&'||link[9]!='='||link[10]!=']')return {};
    uint8_t bytes[5]{};unsigned accumulator=0,bits=0,count=0;
    for(size_t i=2;i<9;++i){
        const int value=Base64(link[i]);if(value<0)return {};
        accumulator=(accumulator<<6)|static_cast<unsigned>(value);bits+=6;
        if(bits>=8){bits-=8;bytes[count++]=static_cast<uint8_t>((accumulator>>bits)&255);}
    }
    // Canonical padding, exact 5-byte map format, nonzero map-point ID.
    if(count!=5||(accumulator&3u)!=0||bytes[0]!=4||!(bytes[1]|bytes[2]|bytes[3]|bytes[4]))return {};
    return link;
}
bool PieUIBridge::CanOpenMap(const std::string&text){return Present()&&!MapLink(text).empty();}
bool PieUIBridge::OpenMap(const std::string&text){
    const auto link=MapLink(text);if(link.empty()||!Present())return false;
    // PieUI's documented PING handler replies with READY synchronously. Never hold a lock across events.
    Probe();if(!Present())return false;
    if(auto send=sender.load()){send(OpenEvent,const_cast<char*>(link.c_str()));return true;}
    return false;
}
