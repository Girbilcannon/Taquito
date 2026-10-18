#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TacoPack {
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Style {
    std::string info, copy, copyMessage;
    float triggerRange = 2.0f, infoRange = 2.0f; // Meters; independent action/text radii.
    bool autoTrigger = false;
    // Original XML values, case-normalized and inherited; unknown fields remain harmless.
    std::unordered_map<std::string, std::string> attributes;
    std::string icon, texture;
    float iconSize = 1, height = 1.5f, alpha = 1, fadeNear = -1, fadeFar = -1;
    float trailScale = 1, animSpeed = 1;
    float mapSize = 20;
    float minSize = 5, maxSize = 2048;
    float stickyX = 0, stickyY = 0, stickyScale = 1;
    float stickyDistance = 350; // Game inches; separate from world fadeNear/fadeFar.
    Vec3 rotation;
    bool hasRotation = false;
    bool scaleWithZoom = true;
    bool inWorld = true, miniMap = true, bigMap = true;
    bool screenSticky = false;
    bool stickyHideOriginal = false;
    uint32_t tint = 0xFFFFFFFF; // RRGGBBAA
};
struct Category {
    Category() = default;
    Category(Category&&) noexcept = default;
    Category& operator=(Category&&) noexcept = default;
    Category(const Category&) = delete;
    Category& operator=(const Category&) = delete;
    std::string key, name;
    std::string copyText, copyMessage; // Explicit category action; not inherited by separator labels.
    Style style;
    bool visible = true;
    bool hidden = false, separator = false;
    bool containsLua = false; // Warning on top-level pack categories only.
    Category* parent = nullptr;
    std::vector<std::unique_ptr<Category>> children;
};
struct Marker { uint32_t map = 0; Vec3 position; std::string guid, asset; Category* category = nullptr; Style style; };
struct Trail { uint32_t map = 0; std::string guid, asset, texture; Category* category = nullptr; Style style; std::vector<std::vector<Vec3>> strips; };
struct Pack {
    Pack() = default;
    Pack(Pack&&) noexcept = default;
    Pack& operator=(Pack&&) noexcept = default;
    Pack(const Pack&) = delete;
    Pack& operator=(const Pack&) = delete;
    std::filesystem::path path;
    std::string name;
    Category root;
    std::unordered_map<std::string, Category*> categories;
    std::vector<Marker> markers;
    std::vector<Trail> trails;
    std::vector<std::string> diagnostics;
    size_t luaFiles = 0;
    bool containsLua = false;
};
std::vector<Pack> LoadFolder(const std::filesystem::path& directory);
bool ExtractAsset(const std::filesystem::path& archive, const std::string& name,
                  std::vector<unsigned char>& bytes, size_t limit = 32 * 1024 * 1024);
bool IsVisible(const Category* category);
bool HasLua(const Style& style);
void DetectLua(Pack& pack);
// Returns the inherited XML value (or nullptr when absent); keys are case-insensitive.
const std::string* Attribute(const Style& style, const std::string& key);
bool StickyWithinDistance(const Style& style, float meters);
}
