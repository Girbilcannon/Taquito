#pragma once
#include "MarkerPack.h"
namespace AttributeRuntime {
std::string CanonicalGuid(const std::string& text);
void Initialize(const std::filesystem::path& directory);
// A missing/loading map must not clear per-map state until a new valid map arrives.
void Tick(uint32_t map,uint32_t shard,uint32_t instance,const std::string& character,int mount,
          double utcSeconds,double animationSeconds);
void UpdateFocus(const std::vector<TacoPack::Pack>& packs,TacoPack::Vec3 player,bool active);
void ResetFocus();
bool MountAllowed(const TacoPack::Style& style);
bool Visible(const TacoPack::Marker& marker);
bool Visible(const TacoPack::Trail& trail);
float BounceOffset(const TacoPack::Marker& marker);
// Preferences are updated by namespace across the loaded packs, never by display label.
bool HasLoadedResetTarget(const TacoPack::Marker& marker,const std::vector<TacoPack::Pack>& packs);
bool Activate(const TacoPack::Marker& marker,const std::vector<TacoPack::Pack>& packs);
// targetGuid is the canonical key, not re-decoded a second time.
bool ClearBehaviorState(const std::string& targetGuid);
void BeginActivationBatch();
void EndActivationBatch();
void ResetBehavior();
std::string Status();
double NextDaily(double utcSeconds);
double NextWeekly(double utcSeconds);
TacoPack::Vec3 RibbonSide(TacoPack::Vec3 direction,bool wall);
}
