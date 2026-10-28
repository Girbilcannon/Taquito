#include "CheckpointRuntime.h"
#include "AttributeRuntime.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <unordered_set>

namespace {
using Marker=TacoPack::Marker;
constexpr size_t None=std::numeric_limits<size_t>::max();
struct Node {
    const Marker*marker=nullptr;std::vector<size_t>next,incoming;
    const TacoPack::Trail*trail=nullptr;std::string trailKey;
    const TacoPack::Style& Style()const{return marker?marker->style:trail->style;}
    const std::string& Key()const{return marker?marker->stateKey:trailKey;}
    uint32_t Map()const{return marker?marker->map:trail->map;}
    const void* Identity()const{return marker?static_cast<const void*>(marker):trail;}
    bool Eligible()const{return marker?AttributeRuntime::Visible(*marker):AttributeRuntime::Visible(*trail);}
};
struct Contact {bool initialized=false,inside=false;};
struct Run {
    size_t root=0;bool blocked=false,finished=false;std::string signature;
    std::vector<size_t>members;
    std::set<size_t>armed,completed;
    std::map<size_t,std::vector<size_t>>forks;
    std::map<size_t,size_t>choices;
    std::unordered_map<size_t,Contact>contact;
};
struct Graph {
    std::string packKey;
    std::vector<Node>nodes;std::unordered_map<std::string,size_t>index;
    std::vector<Run>runs;std::unordered_map<std::string,size_t>roots;
};
struct SavedRun {
    std::string signature;bool finished=false;
    std::vector<std::string>armed,completed;
    std::map<std::string,std::vector<std::string>>forks;
    std::map<std::string,std::string>choices;
};
std::map<std::pair<std::string,std::string>,SavedRun>savedRuns;
std::vector<Graph> graphs;
std::unordered_map<const Marker*,size_t> markerPacks;
std::unordered_map<const void*,uint32_t> paint;
std::unordered_map<uint32_t,std::vector<std::pair<size_t,size_t>>> mapRuns;
uint32_t lastMap=0;bool lastActive=false;TacoPack::Vec3 previousPlayer;
uint64_t contactsPassed=0,resets=0;
double Distance2(TacoPack::Vec3 a,TacoPack::Vec3 b) {
    const double x=static_cast<double>(a.x)-b.x,y=static_cast<double>(a.y)-b.y,z=static_cast<double>(a.z)-b.z;
    return x*x+y*y+z*z;
}
// Closest distance between finite 3D segments, including point segments.
double SegmentDistance2(TacoPack::Vec3 a,TacoPack::Vec3 b,TacoPack::Vec3 c,TacoPack::Vec3 d) {
    const double u[]={double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};
    const double v[]={double(d.x)-c.x,double(d.y)-c.y,double(d.z)-c.z};
    const double w[]={double(a.x)-c.x,double(a.y)-c.y,double(a.z)-c.z};
    auto dot=[](const double*x,const double*y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
    const double aa=dot(u,u),bb=dot(u,v),cc=dot(v,v),dd=dot(u,w),ee=dot(v,w);
    double t=0,q=0;
    if(aa<=1e-20)q=cc>1e-20?std::clamp(ee/cc,0.,1.):0;
    else if(cc<=1e-20)t=std::clamp(-dd/aa,0.,1.);
    else {const double denom=aa*cc-bb*bb;
        if(denom>1e-20)t=std::clamp((bb*ee-cc*dd)/denom,0.,1.);
        q=(bb*t+ee)/cc;
        if(q<0){q=0;t=std::clamp(-dd/aa,0.,1.);}else if(q>1){q=1;t=std::clamp((bb-dd)/aa,0.,1.);}
    }
    double distance=0;for(int i=0;i<3;++i){double delta=w[i]+t*u[i]-q*v[i];distance+=delta*delta;}
    return std::isfinite(distance)?distance:std::numeric_limits<double>::infinity();
}
double ContactDistance2(const Node&node,TacoPack::Vec3 previous,TacoPack::Vec3 player) {
    if(node.marker)return Distance2(node.marker->position,player);
    double nearest=std::numeric_limits<double>::infinity();
    for(const auto&strip:node.trail->strips)for(size_t i=1;i<strip.size();++i)
        if(Distance2(strip[i-1],strip[i])>1e-20)
            nearest=std::min(nearest,SegmentDistance2(previous,player,strip[i-1],strip[i]));
    return nearest;
}
bool Inside(const Node&node,TacoPack::Vec3 player) {
    const double radius=node.Style().cpRadius;
    return ContactDistance2(node,player,player)<=radius*radius;
}
void Repaint(Graph& graph,Run& run) {
    for(auto node:run.members)paint.erase(graph.nodes[node].Identity());
    if(run.blocked||run.finished)return;
    std::unordered_map<size_t,uint32_t>depths;
    for(auto armed:run.armed) {
        const uint32_t horizon=graph.nodes[armed].Style().nextCheckCount;
        std::deque<std::pair<size_t,uint32_t>>queue{{armed,0}};
        std::unordered_map<size_t,uint32_t>visited;
        while(!queue.empty()) {
            auto [node,depth]=queue.front();queue.pop_front();
            if(run.completed.count(node))continue;
            auto found=visited.find(node);if(found!=visited.end()&&found->second<=depth)continue;
            visited[node]=depth;
            auto seen=depths.find(node);if(seen==depths.end()||depth<seen->second)depths[node]=depth;
            if(depth<horizon&&!graph.nodes[node].Style().cpFinish)for(auto next:graph.nodes[node].next)queue.emplace_back(next,depth+1);
        }
    }
    for(auto[node,depth]:depths) {
        const auto&definition=graph.nodes[node];const auto&style=definition.Style();
        const auto base=[&](const char*key,uint32_t value){return !style.checkpoint&&!style.attributes.count(key)?style.tint:value;};
        if(run.armed.count(node))paint[definition.Identity()]=base("cpcolor",style.cpColor);
        else {
            const auto color=base("cpprecolor",style.cpPreColor);
            const double alpha=static_cast<double>(color&255u)*std::pow(.75,depth);
            paint[definition.Identity()]=(color&0xffffff00u)|static_cast<uint32_t>(std::clamp(std::lround(alpha),0l,255l));
        }
    }
}
void Start(Graph&graph,Run&run) {
    run.finished=false;run.armed.clear();run.completed.clear();run.forks.clear();run.choices.clear();run.contact.clear();
    run.armed.insert(run.root);run.contact[run.root]={};Repaint(graph,run);
}
void Complete(Graph&graph,Run&run,size_t node,TacoPack::Vec3 player) {
    // Choosing a fork drops only its frontier alternatives. Repaint keeps reachable rejoins.
    for(auto it=run.forks.begin();it!=run.forks.end();) {
        const auto&alternatives=it->second;
        if(std::find(alternatives.begin(),alternatives.end(),node)!=alternatives.end()) {
            run.choices[it->first]=node;
            for(auto sibling:alternatives)if(sibling!=node){run.armed.erase(sibling);run.contact.erase(sibling);}
            it=run.forks.erase(it);
        }else ++it;
    }
    run.armed.erase(node);run.contact.erase(node);run.completed.insert(node);
    std::vector<size_t>next;
    for(auto successor:graph.nodes[node].next)if(!run.completed.count(successor))next.push_back(successor);
    if(graph.nodes[node].Style().cpFinish||(graph.nodes[node].Style().nextChecks.empty()&&!(graph.nodes[node].Style().checkpointErrors&128u))) {run.finished=true;run.armed.clear();run.contact.clear();run.forks.clear();}
    else {
        if(next.size()>1)run.forks[node]=next;
        for(auto successor:next)if(run.armed.insert(successor).second) {
            const auto&definition=graph.nodes[successor];
            // R::Visible still sees old paint until repaint; category/mount/behavior checked below after repaint.
            run.contact[successor]={true,Inside(definition,player)};
        }
    }
    Repaint(graph,run);
    for(auto successor:next)if(run.armed.count(successor)&&!graph.nodes[successor].Eligible())run.contact[successor].initialized=false;
    ++contactsPassed;
}
std::string Signature(const Graph&graph,const Run&run) {
    auto members=run.members;std::sort(members.begin(),members.end());std::string text;
    for(auto i:members){const auto&node=graph.nodes[i];text+=node.Key()+":"+std::to_string(node.Map())+">";
        for(auto next:node.next)text+=graph.nodes[next].Key()+",";text+=node.Style().cpFinish?"F":"C";text+=node.trail?"T":"M";
        for(const auto&target:node.Style().nextChecks)text+="/"+target;
        text+=(node.Style().checkpointErrors&128u)?"invalid;":";";}return text;
}
void Restore(Graph&graph,Run&run,const SavedRun&saved) {
    run.finished=saved.finished;run.armed.clear();run.completed.clear();run.contact.clear();run.forks.clear();run.choices.clear();
    for(const auto&key:saved.armed){auto node=graph.index.at(key);run.armed.insert(node);run.contact[node]={};}
    for(const auto&key:saved.completed)run.completed.insert(graph.index.at(key));
    for(const auto&[key,values]:saved.forks)for(const auto&value:values)run.forks[graph.index.at(key)].push_back(graph.index.at(value));
    for(const auto&[key,value]:saved.choices)run.choices[graph.index.at(key)]=graph.index.at(value);
    Repaint(graph,run);
}
void DefinitionWarnings(TacoPack::Pack&pack,const TacoPack::Style&style,const std::string&guid) {
    const uint32_t errors=style.checkpointErrors;
    const char* labels[]={"resetguid contains an invalid GUID; invalid entries ignored", "cpReset requires one valid root GUID",
        "cpReset requires a valid triggerRange or autoTrigger=true; reset action disabled", "cpColor must be eight RRGGBBAA hex digits; using FFFFFFFF",
        "cpPreColor must be eight RRGGBBAA hex digits; using FFFFFFFF", "cpRadius must be positive finite meters; using 2",
        "nextCheckCount must be a nonnegative integer; using 2", "nextCheck contains an invalid GUID; invalid links ignored"};
    for(size_t i=0;i<8;++i)if(errors&(1u<<i))pack.diagnostics.push_back("Entry "+(guid.empty()?std::string("(no GUID)"):guid)+": "+labels[i]);
}
}
void CheckpointRuntime::PrepareReload(){
    for(const auto&graph:graphs)for(const auto&run:graph.runs)if(!run.blocked) {
        SavedRun saved;saved.signature=run.signature;saved.finished=run.finished;
        for(auto node:run.armed)saved.armed.push_back(graph.nodes[node].Key());
        for(auto node:run.completed)saved.completed.push_back(graph.nodes[node].Key());
        for(const auto&[node,values]:run.forks)for(auto value:values)saved.forks[graph.nodes[node].Key()].push_back(graph.nodes[value].Key());
        for(auto[node,value]:run.choices)saved.choices[graph.nodes[node].Key()]=graph.nodes[value].Key();
        savedRuns[{graph.packKey,graph.nodes[run.root].Key()}]=std::move(saved);
    }
    graphs.clear();markerPacks.clear();paint.clear();mapRuns.clear();lastMap=0;lastActive=false;
}
void CheckpointRuntime::Clear(){graphs.clear();markerPacks.clear();paint.clear();mapRuns.clear();savedRuns.clear();lastMap=0;lastActive=false;contactsPassed=resets=0;}
void CheckpointRuntime::Load(std::vector<TacoPack::Pack>&packs) {
    PrepareReload();graphs.resize(packs.size());
    std::unordered_set<std::string>knownGuids;
    for(const auto&pack:packs)for(const auto&marker:pack.markers)if(!marker.stateKey.empty())knownGuids.insert(marker.stateKey);
    for(size_t pi=0;pi<packs.size();++pi) {
        auto&pack=packs[pi];auto&graph=graphs[pi];
        auto path=pack.path.lexically_normal().generic_u8string();graph.packKey.assign(reinterpret_cast<const char*>(path.data()),path.size());
        auto identities=pack.guidDefinitions;
        std::unordered_map<std::string,size_t> loadedIdentities;
        for(const auto&marker:pack.markers)if(!marker.stateKey.empty())++loadedIdentities[marker.stateKey];
        for(const auto&trail:pack.trails){auto key=AttributeRuntime::CanonicalGuid(trail.guid);if(!key.empty())++loadedIdentities[key];}
        for(const auto&[key,count]:loadedIdentities)identities[key]=std::max(identities[key],count);
        std::unordered_map<std::string,std::vector<Node>>definitions;
        for(auto&marker:pack.markers) {
            DefinitionWarnings(pack,marker.style,marker.guid);
            for(const auto&target:marker.style.resetGuids)if(!knownGuids.count(target))pack.diagnostics.push_back("resetguid target is not currently loaded; retained without clearing state: "+target);
            if(marker.style.cpResetConfigured)markerPacks[&marker]=pi;
            if(!marker.style.checkpoint&&!marker.style.cpFinish)continue;
            markerPacks[&marker]=pi;
            if(marker.stateKey.empty()){pack.diagnostics.push_back("Checkpoint requires a valid own GUID; marker excluded from native course");continue;}
            if(!std::isfinite(marker.position.x)||!std::isfinite(marker.position.y)||!std::isfinite(marker.position.z)){pack.diagnostics.push_back("Checkpoint has nonfinite coordinates; excluded: "+marker.stateKey);continue;}
            definitions[marker.stateKey].push_back({&marker,{},{}});
        }
        for(const auto&trail:pack.trails)if(trail.style.cpFinish) {
            DefinitionWarnings(pack,trail.style,trail.guid);
            auto key=AttributeRuntime::CanonicalGuid(trail.guid);
            if(key.empty()){pack.diagnostics.push_back("Finish trail requires a valid own GUID; excluded");continue;}
            Node node;node.trail=&trail;node.trailKey=key;definitions[key].push_back(std::move(node));
        }
        std::vector<std::string>keys;
        for(const auto&[key,markers]:definitions) {
            if(markers.size()!=1||identities[key]!=1){pack.diagnostics.push_back("Duplicate checkpoint GUID "+key+"; all definitions excluded from native course");continue;}
            keys.push_back(key);
        }
        std::sort(keys.begin(),keys.end());
        if(keys.size()>100000){keys.resize(100000);pack.diagnostics.push_back("Checkpoint node safety limit (100000) reached; remaining nodes excluded");}
        for(const auto&key:keys){graph.index[key]=graph.nodes.size();graph.nodes.push_back(definitions[key][0]);}
        for(size_t i=0;i<graph.nodes.size();++i) {
            auto&node=graph.nodes[i];
            for(const auto&key:node.Style().nextChecks) {
                auto found=graph.index.find(key);
                if(found==graph.index.end()){pack.diagnostics.push_back("Checkpoint "+node.Key()+": nextCheck target missing, duplicated or not a checkpoint: "+key);continue;}
                auto next=found->second;
                if(graph.nodes[next].Map()!=node.Map()){pack.diagnostics.push_back("Checkpoint "+node.Key()+": cross-map nextCheck link skipped: "+key);continue;}
                if(!node.Style().cpFinish){node.next.push_back(next);graph.nodes[next].incoming.push_back(i);}
            }
        }
        for(const auto&node:graph.nodes)if(!node.Style().cpFinish&&node.next.empty()&&(!node.Style().nextChecks.empty()||(node.Style().checkpointErrors&128u)))
            pack.diagnostics.push_back("Checkpoint has no usable successors; invalid dead end will not finish the run: "+node.Key());
        std::set<size_t> roots;
        for(size_t i=0;i<graph.nodes.size();++i)if(graph.nodes[i].incoming.empty())roots.insert(i);
        for(const auto&marker:pack.markers)if(marker.style.cpResetConfigured&&!marker.style.cpReset.empty()) {
            auto root=graph.index.find(marker.style.cpReset);
            if(root==graph.index.end())pack.diagnostics.push_back("cpReset target is missing, duplicated or not a checkpoint: "+marker.style.cpReset);
            else roots.insert(root->second); // Explicit roots also allow cyclic components.
        }
        std::vector<size_t>owner(graph.nodes.size(),None);
        for(auto root:roots) {
            const size_t ri=graph.runs.size();graph.runs.emplace_back();auto&run=graph.runs.back();run.root=root;
            graph.roots[graph.nodes[root].Key()]=ri;
            std::vector<size_t>stack{root};std::unordered_set<size_t>visited;
            while(!stack.empty()) {
                auto node=stack.back();stack.pop_back();if(!visited.insert(node).second)continue;
                if(owner[node]!=None&&owner[node]!=ri) {
                    run.blocked=true;graph.runs[owner[node]].blocked=true;
                    pack.diagnostics.push_back("Checkpoint run roots share nodes; overlapping runs disabled: "+graph.nodes[root].Key()+" and "+graph.nodes[graph.runs[owner[node]].root].Key());
                    continue; // The existing owner already traversed this shared tail.
                }
                owner[node]=ri;run.members.push_back(node);
                for(auto next:graph.nodes[node].next)stack.push_back(next);
            }
        }
        // Report unrooted cyclic components once, with iterative traversal.
        std::vector<bool>diagnosed(graph.nodes.size(),false);
        for(size_t i=0;i<graph.nodes.size();++i)if(owner[i]==None&&!diagnosed[i]) {
            pack.diagnostics.push_back("Checkpoint component has no natural start or cpReset root; hidden until authored with a root: "+graph.nodes[i].Key());
            std::vector<size_t>stack{i};
            while(!stack.empty()){auto node=stack.back();stack.pop_back();if(diagnosed[node]||owner[node]!=None)continue;diagnosed[node]=true;
                for(auto next:graph.nodes[node].next)stack.push_back(next);for(auto previous:graph.nodes[node].incoming)stack.push_back(previous);}
        }
        for(size_t ri=0;ri<graph.runs.size();++ri){auto&run=graph.runs[ri];if(!run.blocked){
            run.signature=Signature(graph,run);Start(graph,run);
            auto saved=savedRuns.find({graph.packKey,graph.nodes[run.root].Key()});
            if(saved!=savedRuns.end()) {
                if(saved->second.signature==run.signature)Restore(graph,run,saved->second);
                else pack.diagnostics.push_back("Checkpoint course structure changed; new run baseline established: "+graph.nodes[run.root].Key());
            }
            mapRuns[graph.nodes[run.root].Map()].emplace_back(pi,ri);
        }}
    }
}
bool CheckpointRuntime::Visible(const Marker&marker){return !(marker.style.checkpoint||marker.style.cpFinish)||paint.count(&marker)!=0;}
uint32_t CheckpointRuntime::Tint(const Marker&marker,uint32_t ordinary){if(!marker.style.checkpoint&&!marker.style.cpFinish)return ordinary;auto it=paint.find(&marker);return it==paint.end()?ordinary:it->second;}
bool CheckpointRuntime::Visible(const TacoPack::Trail&trail){return !trail.style.cpFinish||paint.count(&trail)!=0;}
uint32_t CheckpointRuntime::Tint(const TacoPack::Trail&trail,uint32_t ordinary){auto it=paint.find(&trail);return it==paint.end()?ordinary:it->second;}
void CheckpointRuntime::UpdateContact(uint32_t map,TacoPack::Vec3 player,bool active) {
    if(!active||!map){lastActive=false;return;}
    const bool baseline=!lastActive||lastMap!=map||!std::isfinite(Distance2(previousPlayer,player))||Distance2(previousPlayer,player)>200.*200.;
    const auto previous=previousPlayer;previousPlayer=player;lastActive=true;lastMap=map;
    auto found=mapRuns.find(map);if(found==mapRuns.end())return;
    size_t chosenPack=None,chosenRun=0,chosenNode=0;double nearest=std::numeric_limits<double>::infinity();std::string chosenKey;
    for(auto[pi,ri]:found->second) {
        auto&graph=graphs[pi];auto&run=graph.runs[ri];if(run.finished)continue;
        for(auto node:run.armed) {
            auto&state=run.contact[node];const auto&definition=graph.nodes[node];
            if(!definition.Eligible()){state={};continue;}
            const bool inside=Inside(definition,player);
            if(baseline||!state.initialized){state={true,inside};continue;}
            const bool entered=!state.inside&&(inside||(definition.trail&&ContactDistance2(definition,previous,player)<=double(definition.Style().cpRadius)*definition.Style().cpRadius));state.inside=inside;
            if(!entered)continue;
            const double distance=ContactDistance2(definition,player,player);
            const auto&key=definition.Key();
            if(distance<nearest||(distance==nearest&&(chosenPack==None||key<chosenKey))) {nearest=distance;chosenPack=pi;chosenRun=ri;chosenNode=node;chosenKey=key;}
        }
    }
    if(chosenPack!=None)Complete(graphs[chosenPack],graphs[chosenPack].runs[chosenRun],chosenNode,player);
}
bool CheckpointRuntime::ResetFrom(const Marker&activator) {
    const auto&s=activator.style;
    if(s.cpReset.empty()||!s.cpResetActivationAllowed)return false;
    auto pack=markerPacks.find(&activator);if(pack==markerPacks.end())return false;
    auto&graph=graphs[pack->second];auto found=graph.roots.find(s.cpReset);if(found==graph.roots.end())return false;
    auto&run=graph.runs[found->second];if(run.blocked)return false;
    AttributeRuntime::BeginActivationBatch();
    for(auto node:run.members)AttributeRuntime::ClearBehaviorState(graph.nodes[node].Key());
    AttributeRuntime::EndActivationBatch();
    Start(graph,run);++resets;return true;
}
CheckpointRuntime::Diagnostics CheckpointRuntime::GetDiagnostics(){
    Diagnostics out;out.contacts=contactsPassed;out.resets=resets;
    for(const auto&graph:graphs){out.nodes+=graph.nodes.size();out.runs+=graph.runs.size();for(const auto&run:graph.runs){out.blockedRuns+=run.blocked?1:0;out.completedRuns+=run.finished?1:0;}}
    return out;
}
