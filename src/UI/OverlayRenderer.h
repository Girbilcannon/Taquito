#pragma once
#include "../Core/MarkerPack.h"
#include "../mumble/Mumble.h"
#include "../nexus/Nexus.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d11.h>

class Renderer {
public:
    bool Initialize(AddonAPI_t* api);
    bool IsReady() const { return device_.Get() != nullptr; }
    size_t WorldTrailSegments() const { return worldTrailSegments_; }
    void Shutdown();
    // UI callbacks may run after Draw queues ImGui commands; release at next frame start.
    void RequestTextureClear();
    void Draw(const std::vector<TacoPack::Pack>& packs, const Mumble::Data* mumble,
              const Mumble::Identity* identity, bool world, bool mini, bool big,
              float miniIconSize, float bigIconSize, float miniTrailSize, float bigTrailSize,
              float worldIconSize, float worldTrailSize,
              bool playerBubble, float bubbleRadius, float bubbleFeather);
private:
    void ClearTextures();
    struct Image { Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view; int width = 0, height = 0; };
    Image* Texture(const TacoPack::Pack& pack, const std::string& name);
    AddonAPI_t* api_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::unordered_map<std::string, Image> images_;
    uint32_t lastMap_ = 0;
    unsigned newTexturesThisFrame_ = 0;
    bool clearTexturesNextFrame_ = false;
    size_t worldTrailSegments_ = 0;
};
