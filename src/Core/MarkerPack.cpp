#include "MarkerPack.h"
#include "MarkerActions.h"
#include "AttributeRuntime.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_HEADER_FILE_ONLY
#include "../third_party/miniz.c"
#include "../third_party/rapidxml.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace TacoPack {
namespace {
using Node = rapidxml::xml_node<>;
using Attr = rapidxml::xml_attribute<>;
std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }
std::string Trim(std::string s) {
    auto first=s.find_first_not_of(" \t\r\n");
    return first==std::string::npos ? std::string{} : s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
std::string Slash(std::string s) { std::replace(s.begin(), s.end(), '\\', '/'); while (s.starts_with("./")) s.erase(0, 2); return s; }
bool Safe(const std::string& s) { return !s.empty() && s.front() != '/' && s.find(':') == std::string::npos && s.find("..") == std::string::npos; }
bool Eq(const char* a, const char* b) { return Lower(a) == Lower(b); }
std::string A(Node* n, const char* key) { if (!n) return {}; for (Attr* a = n->first_attribute(); a; a = a->next_attribute()) if (Eq(a->name(), key)) return a->value(); return {}; }
float Number(const std::string& text, float fallback) { auto s=Trim(text); if(s.empty()) return fallback; try { size_t used=0; float f=std::stof(s,&used); return used==s.size() && std::isfinite(f) ? f : fallback; } catch (...) { return fallback; } }
float F(Node* n, const char* key, float fallback) { return Number(A(n,key),fallback); }
uint32_t U(Node* n, const char* key) { auto s = A(n, key); try { return static_cast<uint32_t>(std::stoul(s)); } catch (...) { return 0; } }
bool B(Node* n, const char* key, bool fallback) { auto s = Lower(Trim(A(n, key))); if (s.empty()) return fallback; if (s == "0" || s == "false") return false; if (s == "1" || s == "true") return true; return fallback; }
uint32_t Color(const std::string& text, uint32_t fallback) {
    auto hex=Trim(text); if(hex.empty()) return fallback;
    const bool prefixed=hex.front()=='#';
    if(prefixed) hex.erase(0,1);
    if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6 && hex.size() != 8) return fallback;
    if (!std::all_of(hex.begin(),hex.end(),[](unsigned char c){ return std::isxdigit(c) != 0; })) return fallback;
    try {
        const auto value=static_cast<uint32_t>(std::stoul(hex,nullptr,16));
        if(hex.size()==6) return (value<<8)|0xFFu;
        // TacO writes bare AARRGGBB. Preserve Taquito's existing #RRGGBBAA form.
        return prefixed ? value : (value<<8)|(value>>24);
    } catch (...) { return fallback; }
}
void Apply(Style& st, Node* n) {
    // Preserve every XML attribute, including attributes used by other pathing tools.
    // A POI/Trail override replaces an inherited category value by name.
    for (Attr* a=n->first_attribute(); a; a=a->next_attribute())
        st.attributes[Lower(a->name())] = a->value();
    if (auto s = A(n, "iconFile"); !s.empty()) st.icon = Slash(s);
    if (auto s = A(n, "texture"); !s.empty()) st.texture = Slash(s);
    st.iconSize = F(n, "iconSize", st.iconSize); st.height = F(n, "heightOffset", st.height);
    st.alpha = std::clamp(F(n, "alpha", st.alpha),0.0f,1.0f); st.fadeNear = F(n, "fadeNear", st.fadeNear);
    st.fadeFar = F(n, "fadeFar", st.fadeFar); st.trailScale = F(n, "trailScale", st.trailScale);
    st.animSpeed = F(n, "animSpeed", st.animSpeed); st.mapSize = F(n, "mapDisplaySize", st.mapSize);
    st.minSize = std::max(0.0f,F(n,"minSize",st.minSize));
    st.maxSize = std::max(st.minSize,F(n,"maxSize",st.maxSize));
    st.tint = Color(A(n,"color"),st.tint);
    st.inWorld = B(n, "inGameVisibility", st.inWorld); st.miniMap = B(n, "miniMapVisibility", st.miniMap);
    st.bigMap = B(n, "mapVisibility", st.bigMap);
    st.scaleWithZoom = B(n, "scaleOnMapWithZoom", st.scaleWithZoom);
    st.screenSticky = B(n, "screenSticky", st.screenSticky);
    st.stickyX = F(n, "stickyX", st.stickyX); st.stickyY = F(n, "stickyY", st.stickyY);
    st.stickyScale = F(n, "stickyScale", st.stickyScale);
    st.stickyDistance = std::max(0.0f, F(n, "stickyDistance", st.stickyDistance));
    st.stickyHideOriginal = B(n, "stickyHideOriginal", st.stickyHideOriginal);
    MarkerActions::Populate(st);
    // Resolve rotation once during loading, including POI overrides of inherited axes.
    const char* names[]={"rotate-x","rotate-y","rotate-z"};
    float* axes[]={&st.rotation.x,&st.rotation.y,&st.rotation.z};
    for(int i=0;i<3;++i) if(!A(n,names[i]).empty()) {
        *axes[i]=F(n,names[i],*axes[i]); st.hasRotation=true;
    }
    if(auto values=A(n,"rotate"); !values.empty()) {
        size_t start=0;
        for(int i=0;i<3 && start<values.size();++i) {
            const auto end=values.find(',',start);
            *axes[i]=Number(values.substr(start,end==std::string::npos ? end : end-start),*axes[i]);
            st.hasRotation=true;
            if(end==std::string::npos) break;
            start=end+1;
        }
    }
}
bool Read(mz_zip_archive& zip, mz_uint i, std::vector<unsigned char>& bytes, size_t limit) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_uncomp_size == 0 || stat.m_uncomp_size > limit || mz_zip_reader_is_file_a_directory(&zip, i)) return false;
    bytes.resize(static_cast<size_t>(stat.m_uncomp_size));
    return mz_zip_reader_extract_to_mem(&zip, i, bytes.data(), bytes.size(), 0) != 0;
}
using ZipIndex = std::unordered_map<std::string, mz_uint>;
int Locate(const ZipIndex& index, const std::string& path) {
    if (!Safe(path)) return -1;
    auto found = index.find(Lower(Slash(path)));
    return found == index.end() ? -1 : static_cast<int>(found->second);
}
ZipIndex Index(mz_zip_archive& zip) {
    ZipIndex result;
    result.reserve(mz_zip_reader_get_num_files(&zip));
    for (mz_uint i=0; i<mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat st{};
        if (mz_zip_reader_file_stat(&zip,i,&st) && !mz_zip_reader_is_file_a_directory(&zip,i))
            result.emplace(Lower(Slash(st.m_filename)),i);
    }
    return result;
}
// Resolve archive-relative assets without extracting a pack; preserve support for an enclosing folder.
std::string Resolve(const ZipIndex& index, const std::string& xmlName, const std::string& ref) {
    std::string clean = Slash(ref);
    if (!Safe(clean)) return {};
    if (Locate(index, clean) >= 0) return clean;
    auto slash = xmlName.find_last_of('/');
    if (slash != std::string::npos) {
        auto candidate = xmlName.substr(0, slash + 1) + clean;
        if (Locate(index, candidate) >= 0) return candidate;
    }
    return {};
}
Category* GetCategory(Pack& pack, const std::string& fullName) {
    auto it = pack.categories.find(Lower(fullName));
    return it == pack.categories.end() ? nullptr : it->second;
}
Category* ClosestAncestor(Pack& pack, std::string type) {
    for (auto dot = type.rfind('.'); dot != std::string::npos; dot = type.rfind('.')) {
        type.resize(dot);
        if (auto* category = GetCategory(pack, type)) return category;
    }
    return nullptr;
}
bool OneEditApart(const std::string& a, const std::string& b) {
    if (a == b || a.size() + 1 < b.size() || b.size() + 1 < a.size()) return false;
    size_t i = 0, j = 0; bool corrected = false;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++i; ++j; continue; }
        if (corrected) return false;
        corrected = true;
        if (a.size() == b.size()) { ++i; ++j; }
        else if (a.size() < b.size()) ++j;
        else ++i;
    }
    return true;
}
Category* NearCategory(Pack& pack, const std::string& type) {
    // A unique one-character XML typo can use its intended category and toggle.
    const auto firstDot = type.find('.');
    if (firstDot == std::string::npos) return nullptr;
    const auto root = type.substr(0, firstDot);
    const auto depth = std::count(type.begin(), type.end(), '.');
    Category* match = nullptr;
    for (const auto& [key, category] : pack.categories) {
        if (key.substr(0, key.find('.')) != root || std::count(key.begin(), key.end(), '.') != depth ||
            !OneEditApart(type, key)) continue;
        if (match) return nullptr; // Ambiguous: do not guess between sibling categories.
        match = category;
    }
    if (match) return match;
    // A misplaced middle segment may still have one exact leaf below the same ancestor.
    auto* ancestor = ClosestAncestor(pack, type);
    if (!ancestor) return nullptr;
    const auto lastDot = type.rfind('.');
    const auto leaf = type.substr(lastDot);
    const auto prefix = ancestor->key + ".";
    for (const auto& [key, category] : pack.categories) {
        if (!key.starts_with(prefix) || !key.ends_with(leaf) ||
            std::count(key.begin(), key.end(), '.') != depth) continue;
        if (match) return nullptr;
        match = category;
    }
    return match;
}
// Build inherited XML styles here; POI-level overrides are applied in Entries below.
void Categories(Pack& pack, Node* parent, Category* root, const std::string& prefix) {
    for (Node* n = parent->first_node(); n; n = n->next_sibling()) {
        if (!Eq(n->name(), "MarkerCategory")) continue;
        auto raw = Lower(A(n, "name")); if (raw.empty()) continue;
        for(char& ch:raw) if(!std::isalnum(static_cast<unsigned char>(ch)) && ch!='.') ch='_';
        Category* current = root; std::string key = prefix;
        size_t start = 0;
        do {
            const auto end = raw.find('.', start);
            auto part = raw.substr(start, end == std::string::npos ? end : end - start);
            if (!part.empty()) {
                if (!key.empty()) key += '.';
                key += part;
                auto found = pack.categories.find(key);
                if (found == pack.categories.end()) {
                    auto child = std::make_unique<Category>(); child->key = key; child->name = part; child->style = current->style; child->parent = current == &pack.root ? nullptr : current;
                    current->children.push_back(std::move(child)); current = current->children.back().get(); pack.categories[key] = current;
                } else current = found->second;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start < raw.size());
        if (auto s = A(n, "DisplayName"); !s.empty()) current->name = s;
        Apply(current->style, n);
        for(Attr* a=n->first_attribute();a;a=a->next_attribute()) {
            if(Eq(a->name(),"copy")) current->copyText=a->value();
            if(Eq(a->name(),"copy-message")) current->copyMessage=a->value();
        }
        current->visible = B(n,"defaultToggle",current->visible);
        current->hidden = B(n,"isHidden",current->hidden);
        current->separator = B(n,"isSeparator",current->separator);
        Categories(pack, n, current, key);
    }
}
// Match POIs and trails to categories, then resolve only their archive asset paths.
void Entries(Pack& pack, mz_zip_archive& zip, const ZipIndex& index, Node* root, const std::string& xmlName,
             std::unordered_map<std::string, Category*>& categoryFallbacks,
             std::unordered_set<std::string>& seenGuids) {
    auto uniqueGuid=[&](uint32_t map, const std::string& guid, int variant) {
        if (guid.empty()) return true; // TacO generates identities when XML omits GUID.
        // Identity is scoped to a map; keep the first instance of an ID.
        auto identity=AttributeRuntime::CanonicalGuid(guid);if(!identity.empty())++pack.guidDefinitions[identity];else identity=guid;
        auto key=std::to_string(map)+":"+identity+":"+std::to_string(variant);
        if (seenGuids.insert(key).second) return true;
        pack.diagnostics.push_back("Duplicate GUID on map " + std::to_string(map) + ": " + guid);
        return false;
    };
    for (Node* parent = root->first_node(); parent; parent = parent->next_sibling()) {
        if (!Eq(parent->name(), "POIs")) continue;
        for (Node* n = parent->first_node(); n; n = n->next_sibling()) {
            const bool isMarker = Eq(n->name(), "POI"), isTrail = Eq(n->name(), "Trail");
            if (!isMarker && !isTrail) continue;
            auto type = Lower(A(n, "type"));
            while (!type.empty() && type.back() == '.') type.pop_back();
            auto* cat = GetCategory(pack, type);
            if (!cat) {
                auto [found, first] = categoryFallbacks.try_emplace(type, nullptr);
                if (first) found->second = NearCategory(pack, type);
                cat = found->second;
                if (!cat) {
                    // TacO accepts entries without a matching category; still read their data.
                    cat = ClosestAncestor(pack, type);
                    const auto message = "Missing category: " + (type.empty() ? std::string("(unnamed entry)") : type) +
                        (cat ? " (rendered under " + cat->key + ")" : " (rendered without a category)");
                    if (std::find(pack.diagnostics.begin(),pack.diagnostics.end(),message)==pack.diagnostics.end())
                        pack.diagnostics.push_back(message);
                }
            }
            if (isMarker) {
                Marker p; p.category = cat; p.style = cat ? cat->style : Style{}; Apply(p.style, n);
                p.map = U(n, "MapID"); p.position = {F(n,"xpos",0), F(n,"ypos",0), F(n,"zpos",0)}; p.guid = A(n,"GUID");
                p.stateKey=AttributeRuntime::CanonicalGuid(p.guid);
                p.asset = Resolve(index, xmlName, p.style.icon);
                if(p.map) {
                    const bool unique=uniqueGuid(p.map,p.guid,p.style.behavior>0 && p.style.invertBehavior ? 1 : 0);
                    // Native duplicate node IDs must reach graph validation, never silently pick a node.
                    if(unique || p.style.checkpoint || p.style.cpFinish)pack.markers.push_back(std::move(p));
                }
            } else if (isTrail) {
                Trail t; t.category = cat; t.style = cat ? cat->style : Style{}; Apply(t.style, n); t.guid = A(n,"GUID");
                t.asset = Resolve(index, xmlName, A(n,"trailData"));
                t.texture = Resolve(index, xmlName, t.style.texture);
                if (t.asset.empty()) { pack.diagnostics.push_back("Missing trail file: " + A(n,"trailData")); continue; }
                int idx = Locate(index, t.asset); std::vector<unsigned char> bytes;
                if (idx < 0 || !Read(zip, static_cast<mz_uint>(idx), bytes, 16 * 1024 * 1024) || bytes.size() < 8 || (bytes.size()-8) % 12) {
                    pack.diagnostics.push_back("Invalid trail data: " + A(n,"trailData")); continue;
                }
                uint32_t version; std::memcpy(&version, bytes.data(), 4); std::memcpy(&t.map, bytes.data()+4, 4);
                if (version != 0 || !t.map) { pack.diagnostics.push_back("Unsupported trail header: " + t.asset); continue; }
                t.strips.emplace_back();
                for (size_t off = 8; off < bytes.size(); off += 12) {
                    Vec3 point; std::memcpy(&point, bytes.data()+off, 12);
                    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) { if (!t.strips.back().empty()) t.strips.emplace_back(); continue; }
                    if (point.x == 0 && point.y == 0 && point.z == 0) { if (!t.strips.back().empty()) t.strips.emplace_back(); }
                    else t.strips.back().push_back(point);
                }
                if (!t.strips.back().size()) t.strips.pop_back();
                const bool unique=uniqueGuid(t.map,t.guid,0);
                if (!t.strips.empty() && (unique || t.style.cpFinish)) pack.trails.push_back(std::move(t));
            }
        }
    }
}
void Load(const std::filesystem::path& path, Pack& pack, std::unordered_set<std::string>& seenGuids) {
    mz_zip_archive zip{}; auto narrow = path.string();
    if (!mz_zip_reader_init_file(&zip, narrow.c_str(), 0)) { pack.diagnostics.push_back("Could not open ZIP archive"); return; }
    const auto index = Index(zip);
    // Inventory only: Lua features are unavailable, but ordinary pack content stays usable.
    for(mz_uint i=0;i<mz_zip_reader_get_num_files(&zip);++i) {
        mz_zip_archive_file_stat stat{};
        if(mz_zip_reader_file_stat(&zip,i,&stat) && !mz_zip_reader_is_file_a_directory(&zip,i) &&
            Lower(Slash(stat.m_filename)).ends_with(".lua")) ++pack.luaFiles;
    }
    struct Doc { std::string name, text; };
    std::vector<Doc> docs; size_t total = 0;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip) && i < 20000; ++i) {
        mz_zip_archive_file_stat stat{}; if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        auto name = Slash(stat.m_filename);
        if (!Lower(name).ends_with(".xml")) continue;
        std::vector<unsigned char> data;
        if (!Read(zip, i, data, 16 * 1024 * 1024)) continue;
        total += data.size(); if (total > 64 * 1024 * 1024) break;
        docs.push_back({name, std::string(data.begin(), data.end())});
    }
    std::unordered_map<std::string, Category*> categoryFallbacks;
    for (int pass = 0; pass < 2; ++pass) for (auto& doc : docs) {
        std::vector<char> mutableXml(doc.text.begin(), doc.text.end()); mutableXml.push_back(0);
        try {
            rapidxml::xml_document<> parsed; parsed.parse<0>(mutableXml.data());
            for (Node* root = parsed.first_node(); root; root = root->next_sibling()) {
                if (!Eq(root->name(), "OverlayData")) continue;
                pack.loaded = true;
                if (pass == 0) Categories(pack, root, &pack.root, "");
                else Entries(pack, zip, index, root, doc.name, categoryFallbacks, seenGuids);
            }
        } catch (const rapidxml::parse_error&) { pack.diagnostics.push_back("Bad XML: " + doc.name); }
    }
    DetectLua(pack);
    mz_zip_reader_end(&zip);
}
}
std::vector<Pack> LoadFolder(const std::filesystem::path& directory) {
    std::vector<Pack> result; std::error_code ec;
    std::unordered_set<std::string> seenGuids; // The current map shares one GUID namespace across packs.
    std::vector<std::filesystem::path> archives;
    std::filesystem::create_directories(directory, ec);
    for (const auto& file : std::filesystem::directory_iterator(directory, ec)) {
        if (!file.is_regular_file()) continue;
        auto ext = Lower(file.path().extension().string()); if (ext != ".zip" && ext != ".taco") continue;
        archives.push_back(file.path());
    }
    std::sort(archives.begin(), archives.end());
    for (const auto& path : archives) {
        Pack pack; pack.path = path; pack.name = path.stem().string(); pack.root.name = pack.name;
        Load(path, pack, seenGuids); result.push_back(std::move(pack));
    }
    std::sort(result.begin(), result.end(), [](const Pack& a, const Pack& b) { return a.name < b.name; });
    return result;
}
bool ExtractAsset(const std::filesystem::path& archive, const std::string& name, std::vector<unsigned char>& bytes, size_t limit) {
    if (!Safe(name)) return false;
    mz_zip_archive zip{}; auto narrow = archive.string();
    if (!mz_zip_reader_init_file(&zip, narrow.c_str(), 0)) return false;
    // miniz has its own central-directory index; avoid building a second map for one asset.
    int idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (idx < 0) idx = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    bool ok = idx >= 0 && Read(zip, static_cast<mz_uint>(idx), bytes, limit);
    mz_zip_reader_end(&zip); return ok;
}
bool IsVisible(const Category* c) { for (; c; c = c->parent) if (!c->visible) return false; return true; }
bool HasLua(const Style& style) {
    for(const char* key:{"script-filter","script-tick","script-focus","script-trigger","script-once"}) {
        auto value=style.attributes.find(key);
        if(value!=style.attributes.end() && !Trim(value->second).empty()) return true;
    }
    return false;
}
void DetectLua(Pack& pack) {
    pack.containsLua=pack.luaFiles>0;
    for(const auto& [key,category]:pack.categories) pack.containsLua |= HasLua(category->style);
    for(const auto& marker:pack.markers) pack.containsLua |= HasLua(marker.style);
    for(const auto& trail:pack.trails) pack.containsLua |= HasLua(trail.style);
    // All top-level categories supplied by this archive carry the same pack warning.
    for(auto& category:pack.root.children) category->containsLua=pack.containsLua;
}
const std::string* Attribute(const Style& style, const std::string& key) {
    auto found=style.attributes.find(Lower(key));
    if(found==style.attributes.end() && Lower(key)=="toggle") found=style.attributes.find("togglecategory");
    return found==style.attributes.end() ? nullptr : &found->second;
}
bool StickyWithinDistance(const Style& style, float meters) {
    return style.screenSticky && std::isfinite(meters) && meters >= 0.0f &&
           meters <= style.stickyDistance * 0.0254f;
}
}
