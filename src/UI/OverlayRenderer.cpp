#include "OverlayRenderer.h"
#include "../Core/VisualAttributes.h"
#include "../imgui/imgui.h"
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
using TacoPack::Vec3;
constexpr float Pi = 3.14159265358979323846f;
constexpr float DefaultFovRadians = 0.872664626f; // Temporary vertical FOV until Mumble provides a live value.
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 Lerp(Vec3 a, Vec3 b, float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 Normal(Vec3 a) { float n = std::sqrt(Dot(a,a)); return n > 0.0001f ? Vec3{a.x/n,a.y/n,a.z/n} : Vec3{}; }
Vec3 V(Mumble::Vector3 p) { return {p.X,p.Y,p.Z}; }
float UIScale(Mumble::EUIScale v) {
    switch(v) { case Mumble::EUIScale::Small: return .9f; case Mumble::EUIScale::Large: return 1.111f; case Mumble::EUIScale::Larger: return 1.224f; default: return 1; }
}
struct Projection {
    Vec3 cam, forward, right, up;
    float cx = 0, cy = 0, focal = 0;
    bool valid = false;
    float Depth(Vec3 p) const { return Dot(Sub(p,cam),forward); }
    bool Screen(Vec3 p, ImVec2& out) const {
        auto rel = Sub(p, cam); float z = Dot(rel,forward);
        if (!valid || z <= .05f) return false;
        out = {cx + Dot(rel,right)*focal/z, cy - Dot(rel,up)*focal/z};
        return std::isfinite(out.x) && std::isfinite(out.y);
    }
};
bool ClipNear(const Projection& proj, Vec3& a, Vec3& b) {
    constexpr float nearDepth = .051f;
    const float da = proj.Depth(a), db = proj.Depth(b);
    if (da <= nearDepth && db <= nearDepth) return false;
    if (da <= nearDepth) a = Lerp(a, b, (nearDepth-da)/(db-da));
    else if (db <= nearDepth) b = Lerp(a, b, (da-nearDepth)/(da-db));
    return true;
}
struct MapProjection {
    ImVec2 center, min, max;
    float scale = 0, rotation = 0;
    Vec3 avatar;
    Mumble::Vector2 offset{};
    bool valid = false;
    ImVec2 Screen(Vec3 p) const {
        const float units = 1.0f / (.0254f * 24.0f);
        float dx = (p.x-avatar.x)*units - offset.X;
        float dy = -(p.z-avatar.z)*units - offset.Y;
        float c=std::cos(rotation), s=std::sin(rotation);
        return {center.x+(dx*c-dy*s)*scale,center.y+(dx*s+dy*c)*scale};
    }
    bool Inside(ImVec2 p, float margin = 0) const { return p.x >= min.x-margin && p.y >= min.y-margin && p.x <= max.x+margin && p.y <= max.y+margin; }
};
float Alpha(const TacoPack::Style& style, float meters) {
    return VisualAttributes::Opacity(style,meters);
}
float Bubble(Vec3 point, Vec3 avatar, bool enabled, float radius, float feather) {
    if (!enabled) return 1.0f;
    // The center sits at torso height so markers and trails directly behind
    // the character fade without clearing unrelated geometry high overhead.
    avatar.y+=1.0f;
    const Vec3 delta=Sub(point,avatar);
    const float distance=std::sqrt(Dot(delta,delta));
    const float inner=std::max(0.0f,radius-feather);
    const float t=std::clamp((distance-inner)/std::max(feather,.01f),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
ImU32 Tint(const TacoPack::Style& style,float a) {
    const auto c=style.tint;
    const float opacity=std::clamp(a,0.0f,1.0f)*static_cast<float>(c&255u)/255.0f;
    return IM_COL32((c>>24)&255u,(c>>16)&255u,(c>>8)&255u,static_cast<int>(opacity*255));
}
bool Clip(ImVec2& a, ImVec2& b, ImVec2 mn, ImVec2 mx) {
    float t0=0,t1=1,dx=b.x-a.x,dy=b.y-a.y;
    auto edge=[&](float p,float q) { if (std::abs(p)<1e-6f) return q>=0; float r=q/p; if(p<0) t0=std::max(t0,r); else t1=std::min(t1,r); return t0<=t1; };
    if(!edge(-dx,a.x-mn.x)||!edge(dx,mx.x-a.x)||!edge(-dy,a.y-mn.y)||!edge(dy,mx.y-a.y)) return false;
    ImVec2 orig=a; b={orig.x+t1*dx,orig.y+t1*dy}; a={orig.x+t0*dx,orig.y+t0*dy}; return true;
}
void Ribbon(ImDrawList* draw, ImVec2 a, ImVec2 b, float width, ImU32 color,
            ID3D11ShaderResourceView* texture, int imageWidth, int imageHeight,
            double phase, unsigned& budget) {
    float dx=b.x-a.x,dy=b.y-a.y,n=std::sqrt(dx*dx+dy*dy); if(n<.1f) return;
    float nx=-dy/n*width*.5f,ny=dx/n*width*.5f;
    if (!texture || imageWidth<=0 || imageHeight<=0 || !budget) {
        draw->AddQuadFilled({a.x+nx,a.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{a.x-nx,a.y-ny},color);
        return;
    }
    const double tile=VisualAttributes::TileLength(width,imageWidth,imageHeight,1.0);
    double from=0;
    while (from<n && budget) {
        const double offset=std::fmod(std::fmod(phase+from,tile)+tile,tile);
        // A rounded tile boundary must never leave 'from' unchanged.
        const double to=std::min(static_cast<double>(n),from+std::max(.1,tile-offset));
        if (!(to>from)) break;
        const float v0=static_cast<float>(1.0-offset/tile);
        const float v1=static_cast<float>(1.0-(offset+to-from)/tile);
        ImVec2 p{a.x+dx*static_cast<float>(from/n),a.y+dy*static_cast<float>(from/n)};
        ImVec2 q{a.x+dx*static_cast<float>(to/n),a.y+dy*static_cast<float>(to/n)};
        draw->AddImageQuad(reinterpret_cast<ImTextureID>(texture),
            {p.x+nx,p.y+ny},{q.x+nx,q.y+ny},{q.x-nx,q.y-ny},{p.x-nx,p.y-ny},
            {0,v0},{0,v1},{1,v1},{1,v0},color);
        from=to;
        --budget;
    }
    if (from<n) {
        ImVec2 p{a.x+dx*static_cast<float>(from/n),a.y+dy*static_cast<float>(from/n)};
        draw->AddQuadFilled({p.x+nx,p.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{p.x-nx,p.y-ny},color);
    }
}
void GroundRibbon(ImDrawList* draw, const Projection& proj, Vec3 avatar, Vec3 a, Vec3 b,
                  float width, const TacoPack::Style& style, ID3D11ShaderResourceView* texture,
                  int imageWidth, int imageHeight, double phase, unsigned& budget,
                  ImVec2 display, bool bubbleEnabled, float bubbleRadius, float bubbleFeather) {
    const Vec3 delta=Sub(b,a);
    const double length=std::sqrt(static_cast<double>(Dot(delta,delta)));
    if (!(length>.001) || !std::isfinite(length)) return;
    if (style.fadeNear>=0 && style.fadeFar>style.fadeNear) {
        const float t=std::clamp(Dot(Sub(avatar,a),delta)/Dot(delta,delta),0.0f,1.0f);
        const Vec3 nearest=Lerp(a,b,t), rel=Sub(nearest,avatar);
        if (std::sqrt(Dot(rel,rel)) >= style.fadeFar*.0254f) return;
    }
    Vec3 horizontal{delta.x,0,delta.z};
    Vec3 ort=Normal(Cross(horizontal,{0,1,0}));
    if (Dot(ort,ort)<.5f) return;
    ort={ort.x*width*.5f,0,ort.z*width*.5f};
    auto opacity=[&](Vec3 point) {
        const Vec3 rel=Sub(point,avatar);
        return Alpha(style,std::sqrt(Dot(rel,rel))) *
               Bubble(point,avatar,bubbleEnabled,bubbleRadius,bubbleFeather);
    };
    auto flat=[&](double from,double to) {
        const Vec3 p=Lerp(a,b,static_cast<float>(from/length));
        const Vec3 q=Lerp(a,b,static_cast<float>(to/length));
        const float alpha=opacity(Lerp(p,q,.5f));
        if (alpha<=.01f) return;
        const ImU32 color=Tint(style,alpha);
        ImVec2 p1,p2,p3,p4;
        if (!proj.Screen({p.x+ort.x,p.y+ort.y,p.z+ort.z},p1) ||
            !proj.Screen({q.x+ort.x,q.y+ort.y,q.z+ort.z},p2) ||
            !proj.Screen({q.x-ort.x,q.y-ort.y,q.z-ort.z},p3) ||
            !proj.Screen({p.x-ort.x,p.y-ort.y,p.z-ort.z},p4)) return;
        const float left=std::min({p1.x,p2.x,p3.x,p4.x}),right=std::max({p1.x,p2.x,p3.x,p4.x});
        const float top=std::min({p1.y,p2.y,p3.y,p4.y}),bottom=std::max({p1.y,p2.y,p3.y,p4.y});
        if(right<0 || left>display.x || bottom<0 || top>display.y) return;
        if (!texture || !budget || imageWidth<=0 || imageHeight<=0) {
            draw->AddQuadFilled(p1,p2,p3,p4,color);
            return;
        }
        const double tile=VisualAttributes::TileLength(width,imageWidth,imageHeight,.25);
        const double offset=std::fmod(std::fmod(phase+from,tile)+tile,tile);
        const float v0=static_cast<float>(1.0-offset/tile);
        const float v1=static_cast<float>(1.0-(offset+to-from)/tile);
        draw->AddImageQuad(reinterpret_cast<ImTextureID>(texture),p1,p2,p3,p4,
                           {0,v0},{0,v1},{1,v1},{1,v0},color);
        --budget;
    };
    auto solidRemainder=[&](double from) {
        if (from>=length) return;
        std::vector<double> cuts{from,length};
        const auto sphereCuts=[&](double radius, Vec3 center) {
            if (radius<0) return;
            const Vec3 offset=Sub(a,center);
            const double aa=Dot(delta,delta),bb=2.0*Dot(offset,delta);
            const double cc=Dot(offset,offset)-radius*radius;
            const double discriminant=bb*bb-4*aa*cc;
            if(discriminant<0) return;
            const double root=std::sqrt(discriminant);
            for(double u:{(-bb-root)/(2*aa),(-bb+root)/(2*aa)}) {
                const double position=u*length;
                if(position>from && position<length) cuts.push_back(position);
            }
        };
        sphereCuts(style.fadeNear<0 ? -1 : style.fadeNear*.0254,avatar);
        sphereCuts(style.fadeFar<0 ? -1 : style.fadeFar*.0254,avatar);
        if(bubbleEnabled) {
            Vec3 center=avatar; center.y+=1.0f;
            sphereCuts(std::max(0.0f,bubbleRadius-bubbleFeather),center);
            sphereCuts(bubbleRadius,center);
        }
        std::sort(cuts.begin(),cuts.end());
        ID3D11ShaderResourceView* saved=texture;
        texture=nullptr;
        for(size_t i=1;i<cuts.size();++i) {
            const double middle=(cuts[i-1]+cuts[i])*.5;
            const Vec3 point=Lerp(a,b,static_cast<float>(middle/length));
            const float alpha=opacity(point);
            const float fraction=style.alpha>.001f ? alpha/style.alpha : 0.0f;
            const unsigned steps=fraction>.01f && fraction<.99f ? 8 : 1;
            for(unsigned j=0;j<steps;++j)
                flat(cuts[i-1]+(cuts[i]-cuts[i-1])*j/steps,
                     cuts[i-1]+(cuts[i]-cuts[i-1])*(j+1)/steps);
        }
        texture=saved;
    };
    if (!texture || !budget || imageWidth<=0 || imageHeight<=0) { solidRemainder(0); return; }
    const double tile=VisualAttributes::TileLength(width,imageWidth,imageHeight,.25);
    double from=0;
    unsigned iteration=0;
    while (from<length && budget && iteration++<512) {
        const double offset=std::fmod(std::fmod(phase+from,tile)+tile,tile);
        const double to=std::min(length,from+std::max(.001,tile-offset));
        if (!(to>from)) break;
        flat(from,to);
        from=to;
    }
    if (from<length) solidRemainder(from);
}
}

bool Renderer::Initialize(AddonAPI_t* api) {
    api_=api; auto* swap = static_cast<IDXGISwapChain*>(api->SwapChain);
    return swap && SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(device_.GetAddressOf())));
}
void Renderer::ClearTextures() { images_.clear(); }
void Renderer::RequestTextureClear() { clearTexturesNextFrame_ = true; }
void Renderer::Shutdown() { ClearTextures(); device_.Reset(); api_=nullptr; }
Renderer::Image* Renderer::Texture(const TacoPack::Pack& pack, const std::string& name) {
    if (!device_ || name.empty()) return nullptr;
    auto key=pack.path.string()+"|"+name; auto found=images_.find(key); if(found!=images_.end()) return found->second.view ? &found->second : nullptr;
    if (newTexturesThisFrame_ >= 2) return nullptr;
    ++newTexturesThisFrame_;
    Image image;
    std::vector<unsigned char> bytes;
    if (TacoPack::ExtractAsset(pack.path,name,bytes)) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        // WIC COM initialization is per-thread; Nexus's render thread may not initialize COM.
        const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
           SUCCEEDED(factory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size()))) &&
           SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder)) &&
           SUCCEEDED(decoder->GetFrame(0,&frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
           SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) {
            UINT w=0,h=0; converter->GetSize(&w,&h);
            if(w && h && w<=4096 && h<=4096) {
                std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);
                if(SUCCEEDED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()))) {
                    D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
                    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels.data(); data.SysMemPitch=w*4;
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                    if(SUCCEEDED(device_->CreateTexture2D(&desc,&data,&texture)) && SUCCEEDED(device_->CreateShaderResourceView(texture.Get(),nullptr,&image.view))) {
                        image.width=static_cast<int>(w); image.height=static_cast<int>(h);
                    }
                }
            }
        }
        if(SUCCEEDED(init)) CoUninitialize();
    }
    auto [it,inserted]=images_.emplace(key,std::move(image)); return it->second.view ? &it->second : nullptr;
}
void Renderer::Draw(const std::vector<TacoPack::Pack>& packs, const Mumble::Data* m,
                    const Mumble::Identity* identity, bool world, bool mini, bool big,
                    float miniIconSize, float bigIconSize, float miniTrailSize, float bigTrailSize,
                    float worldIconSize, float worldTrailSize,
                    bool playerBubble, float bubbleRadius, float bubbleFeather) {
    // ImGui stores raw SRV pointers in its draw list. A menu click later in the
    // same frame must not release a texture until those commands are consumed.
    if (clearTexturesNextFrame_) { ClearTextures(); clearTexturesNextFrame_ = false; }
    worldTrailSegments_ = 0;
    if(!m || !m->UITick || !device_) return;
    const auto map=m->Context.MapID; if(!map) return;
    // Map changes release privately owned textures before the next map loads them on demand.
    if(map != lastMap_) { ClearTextures(); lastMap_=map; }
    newTexturesThisFrame_ = 0;
    unsigned worldTileBudget = 4096, mapTileBudget = 4096; // Bounded independently.
    auto io=ImGui::GetIO(); auto* draw=ImGui::GetBackgroundDrawList();
    const Vec3 avatar=V(m->AvatarPosition),cam=V(m->CameraPosition);
    Projection proj; proj.cam=cam; proj.forward=Normal(V(m->CameraFront));
    proj.right=Normal(Cross({0,1,0},proj.forward)); proj.up=Normal(Cross(proj.forward,proj.right));
    proj.cx=io.DisplaySize.x*.5f; proj.cy=io.DisplaySize.y*.5f;
    // Use the live vertical FOV for world projection whenever Mumble provides it.
    const float fov = identity && std::isfinite(identity->FOV) && identity->FOV > .1f && identity->FOV < Pi-.1f
        ? identity->FOV : DefaultFovRadians;
    proj.focal=io.DisplaySize.y*.5f/std::tan(fov*.5f);
    proj.valid=Dot(proj.forward,proj.forward)>.5f && Dot(proj.right,proj.right)>.5f;
    const float ui=identity ? UIScale(identity->UISize) : 1;
    const auto& c=m->Context.Compass;
    MapProjection mp;
    if(c.Scale>.001f && c.Width>0 && c.Height>0) {
        mp.avatar=avatar; mp.scale=ui/c.Scale;
        mp.rotation=m->Context.IsCompassRotating && !m->Context.IsMapOpen ? c.Rotation : 0;
        mp.offset={c.Center.X-c.PlayerPosition.X,c.Center.Y-c.PlayerPosition.Y};
        if(m->Context.IsMapOpen) { mp.min={0,0}; mp.max=io.DisplaySize; }
        else { float w=c.Width*ui,h=c.Height*ui; float bottom=(identity && identity->UISize==Mumble::EUIScale::Small ? 33.0f:37.0f)*ui;
            mp.min={io.DisplaySize.x-w,m->Context.IsCompassTopRight ? 1.0f : io.DisplaySize.y-h-bottom};
            mp.max={io.DisplaySize.x,mp.min.y+h}; }
        mp.center={(mp.min.x+mp.max.x)*.5f,(mp.min.y+mp.max.y)*.5f}; mp.valid=true;
    }
    const float mapIconMultiplier = m->Context.IsMapOpen ? bigIconSize : miniIconSize;
    const float mapTrailMultiplier = m->Context.IsMapOpen ? bigTrailSize : miniTrailSize;
    const auto mapDisplaySize = [&](const TacoPack::Style& style,float multiplier) {
        const float zoom = style.scaleWithZoom ? 1.0f/c.Scale : 1.0f;
        return style.mapSize * zoom * ui * multiplier;
    };
    // Pick the screen copy before drawing world markers so only the displayed
    // copy can hide its original. World fade is independent of sticky range.
    const TacoPack::Pack* stickyPack = nullptr;
    const TacoPack::Marker* stickyMarker = nullptr;
    float stickyNearestMeters = std::numeric_limits<float>::infinity();
    Image* stickyImage = nullptr;
    if (world && !m->Context.IsMapOpen && io.DisplaySize.x>0 && io.DisplaySize.y>0) {
        for (const auto& pack : packs) for (const auto& p : pack.markers) {
            if (p.map!=map || !p.style.inWorld || !TacoPack::IsVisible(p.category) || p.asset.empty() ||
                std::clamp(p.style.alpha,0.0f,1.0f)<=.01f || (p.style.tint&255u)==0) continue;
            const Vec3 delta=Sub(p.position,avatar);
            const float distance=std::sqrt(Dot(delta,delta));
            if (distance<stickyNearestMeters && TacoPack::StickyWithinDistance(p.style,distance)) {
                stickyNearestMeters=distance; stickyPack=&pack; stickyMarker=&p;
            }
        }
        if (stickyMarker && stickyPack) stickyImage=Texture(*stickyPack,stickyMarker->asset);
        // A missing or not-yet-loaded screen image must not hide its original.
        if (!stickyImage) { stickyPack=nullptr; stickyMarker=nullptr; }
    }
    const TacoPack::Marker* hoveredMarker = nullptr;
    for(const auto& pack:packs) {
        for(const auto& t:pack.trails) {
            if(t.map!=map || !TacoPack::IsVisible(t.category)) continue;
            bool inWorld=world && proj.valid && t.style.inWorld && !m->Context.IsMapOpen;
            bool onMap=mp.valid && (m->Context.IsMapOpen ? big && t.style.bigMap : mini && t.style.miniMap);
            if(!inWorld && !onMap) continue;
            Image* img=Texture(pack,t.texture); auto* texture=img ? img->view.Get():nullptr;
            // The map clip applies only to map geometry. Applying it before
            // the world pass clipped every visible world trail to the compass.
            if(inWorld) for(const auto& strip:t.strips) {
                double phase=0;
                for(size_t i=1;i<strip.size();++i) {
                    Vec3 start=strip[i-1], end=strip[i];
                    if(!ClipNear(proj,start,end)) continue;
                    ImVec2 a{},b{};
                    if(!proj.Screen(start,a) || !proj.Screen(end,b)) continue;
                    const double fullLength=std::sqrt(static_cast<double>(Dot(Sub(strip[i],strip[i-1]),Sub(strip[i],strip[i-1]))));
                    if(t.style.alpha>.01f && Clip(a,b,{0,0},io.DisplaySize)) {
                        ++worldTrailSegments_;
                        // A horizontal cross-vector keeps the ribbon's face up
                        // while its four corners are projected independently.
                        const float width=std::clamp(1.016f*t.style.trailScale*worldTrailSize,.1f,10.0f);
                        const double tile=VisualAttributes::TileLength(width,img ? img->width : 0,img ? img->height : 0,.25);
                        GroundRibbon(draw,proj,avatar,start,end,width,
                            t.style,texture,img ? img->width : 0,img ? img->height : 0,
                            phase+std::sqrt(static_cast<double>(Dot(Sub(start,strip[i-1]),Sub(start,strip[i-1]))))
                                + VisualAttributes::AnimationOffset(ImGui::GetTime(),t.style.animSpeed,tile),
                            worldTileBudget,io.DisplaySize,playerBubble,bubbleRadius,bubbleFeather);
                    }
                    if(std::isfinite(fullLength)) phase+=fullLength;
                }
            }
            if(onMap) {
                draw->PushClipRect(mp.min,mp.max,true);
                for(const auto& strip:t.strips) {
                    double phase=0;
                    for(size_t i=1;i<strip.size();++i) {
                        ImVec2 a=mp.Screen(strip[i-1]), b=mp.Screen(strip[i]);
                        const ImVec2 original=a;
                        const float fullLength=std::hypot(b.x-a.x,b.y-a.y);
                        if(Clip(a,b,mp.min,mp.max)) {
                            const float width=VisualAttributes::MapTrailWidth(mapDisplaySize(t.style,mapTrailMultiplier),t.style.trailScale);
                            const double tile=VisualAttributes::TileLength(width,img ? img->width : 0,img ? img->height : 0,1.0);
                            Ribbon(draw,a,b,width,
                                Tint(t.style,t.style.alpha),texture,img ? img->width : 0,img ? img->height : 0,
                                phase+std::hypot(a.x-original.x,a.y-original.y)
                                    + VisualAttributes::AnimationOffset(ImGui::GetTime(),t.style.animSpeed,tile),mapTileBudget);
                        }
                        if(std::isfinite(fullLength)) phase+=fullLength;
                    }
                }
                draw->PopClipRect();
            }
        }
        for(const auto& p:pack.markers) {
            if(p.map!=map || !TacoPack::IsVisible(p.category)) continue;
            bool inWorld=world && proj.valid && p.style.inWorld && !m->Context.IsMapOpen &&
                !(&p==stickyMarker && p.style.stickyHideOriginal);
            bool onMap=mp.valid && (m->Context.IsMapOpen ? big && p.style.bigMap : mini && p.style.miniMap);
            if(!inWorld && !onMap) continue;
            Image* image=Texture(pack,p.asset);
            auto drawMarker=[&](ImVec2 pos,float half,float alpha) {
                if(alpha<=.01f) return;
                if(image) draw->AddImage(reinterpret_cast<ImTextureID>(image->view.Get()),{pos.x-half,pos.y-half},{pos.x+half,pos.y+half},{0,0},{1,1},Tint(p.style,alpha));
                else draw->AddCircleFilled(pos,std::max(half,3.0f),Tint(p.style,alpha));
            };
            if(inWorld) {
                Vec3 point=p.position; point.y+=p.style.height;
                ImVec2 screen; float depth=proj.Depth(point);
                const float opacity=Alpha(p.style,std::sqrt(Dot(Sub(point,avatar),Sub(point,avatar)))) *
                    Bubble(point,avatar,playerBubble,bubbleRadius,bubbleFeather);
                if (image && opacity>.01f && p.style.hasRotation && depth>.05f) {
                    // Fixed orientation in world space; screenSticky uses its own pixel placement.
                    float half=std::clamp(proj.focal*.5f*p.style.iconSize*worldIconSize/depth,
                        p.style.minSize,p.style.maxSize)*depth/proj.focal;
                    ImVec2 corners[4];
                    const Vec3 local[4]={{-half,half,0},{half,half,0},{half,-half,0},{-half,-half,0}};
                    bool valid=true;
                    for (int i=0;i<4;++i) {
                        const Vec3 r=VisualAttributes::Rotate(local[i],p.style.rotation);
                        valid &= proj.Screen({point.x+r.x,point.y+r.y,point.z+r.z},corners[i]);
                    }
                    if (valid) draw->AddImageQuad(reinterpret_cast<ImTextureID>(image->view.Get()),
                        corners[0],corners[1],corners[2],corners[3],{0,0},{1,0},{1,1},{0,1},Tint(p.style,opacity));
                } else if(proj.Screen(point,screen) && screen.x>=-200 && screen.y>=-200 && screen.x<=io.DisplaySize.x+200 && screen.y<=io.DisplaySize.y+200)
                    drawMarker(screen,std::clamp(proj.focal*.5f*p.style.iconSize*worldIconSize/depth,
                        p.style.minSize,p.style.maxSize),opacity);
            }
            if(onMap) {
                ImVec2 screen=mp.Screen(p.position);
                if(mp.Inside(screen,40)) { draw->PushClipRect(mp.min,mp.max,true);
                    float half=std::clamp(mapDisplaySize(p.style,mapIconMultiplier)*.5f,1.0f,256.0f);
                    drawMarker(screen,half,p.style.alpha); draw->PopClipRect();
                    if (io.MousePos.x>=screen.x-half && io.MousePos.x<=screen.x+half &&
                        io.MousePos.y>=screen.y-half && io.MousePos.y<=screen.y+half && mp.Inside(io.MousePos))
                        hoveredMarker=&p;
                }
            }
        }
    }
    if (hoveredMarker) {
        const auto* title=TacoPack::Attribute(hoveredMarker->style,"tip-name");
        const auto* detail=TacoPack::Attribute(hoveredMarker->style,"tip-description");
        // An explicitly empty title disables the tooltip, as in Pathing.
        if ((!title || !title->empty()) && ((title && !title->empty()) ||
            (detail && !detail->empty()) || hoveredMarker->category)) {
            ImGui::BeginTooltip();
            const char* heading=title ? title->c_str() :
                (hoveredMarker->category ? hoveredMarker->category->name.c_str() : "Marker");
            ImGui::TextUnformatted(heading);
            if (detail && !detail->empty()) { ImGui::Separator(); ImGui::TextWrapped("%s",detail->c_str()); }
            ImGui::EndTooltip();
        }
    }
    if(stickyMarker && stickyImage) {
        Image* image=stickyImage;
        if(image && io.DisplaySize.x > 0 && io.DisplaySize.y > 0) {
            const auto& style=stickyMarker->style;
            const float scale=std::clamp(style.stickyScale,0.01f,10.0f);
            float width=image->width*scale, height=image->height*scale;
            // Keep the image fully on-screen without distorting its aspect ratio.
            const float margin=std::min(12.0f,std::min(io.DisplaySize.x,io.DisplaySize.y)*.05f);
            const float availableWidth=io.DisplaySize.x-2.0f*margin;
            const float availableHeight=io.DisplaySize.y-2.0f*margin;
            const float fit=std::min({1.0f,availableWidth/width,availableHeight/height});
            width*=fit; height*=fit;
            const float x=std::clamp(io.DisplaySize.x*.5f+style.stickyX,
                margin+width*.5f,io.DisplaySize.x-margin-width*.5f);
            const float y=std::clamp(io.DisplaySize.y*.5f-style.stickyY,
                margin+height*.5f,io.DisplaySize.y-margin-height*.5f);
            draw->AddImage(reinterpret_cast<ImTextureID>(image->view.Get()),
                {x-width*.5f,y-height*.5f},{x+width*.5f,y+height*.5f},
                {0,0},{1,1},Tint(style,style.alpha));
        }
    }
}
