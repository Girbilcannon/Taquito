#pragma once
#include "MarkerPack.h"
namespace CheckpointRuntime {
// UI thread only. Rebuild only after pack adoption; no graph traversal in rendering.
void Load(std::vector<TacoPack::Pack>& packs);
// Capture ID-based progress before old pack objects are destroyed.
void PrepareReload();
void Clear();
bool Visible(const TacoPack::Marker& marker);
uint32_t Tint(const TacoPack::Marker& marker,uint32_t ordinary);
// Native contact never substitutes for manual/auto activation of reset actions.
bool Visible(const TacoPack::Trail& trail);
uint32_t Tint(const TacoPack::Trail& trail,uint32_t ordinary);
void UpdateContact(uint32_t map,TacoPack::Vec3 player,bool active);
bool ResetFrom(const TacoPack::Marker& activator);
struct Diagnostics { size_t nodes=0,runs=0,blockedRuns=0,completedRuns=0;uint64_t contacts=0,resets=0; };
Diagnostics GetDiagnostics();
}
