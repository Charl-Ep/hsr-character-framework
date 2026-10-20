#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace hksr::framework {
namespace {
void Need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool Finite(api::Position3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
SceneLayout CaptureSceneLayout(const SceneCharacters& registry,const std::string& scene) {
    SceneLayout result;result.scene=scene;
    for(auto id:registry.Ids()) {
        const auto status=registry.Status(id);
        if(status.scene!=scene || status.deleting)continue;
        const auto& plan=registry.Plan(id);
        if(plan.persistence==CharacterPersistence::Session)continue;
        SavedCharacter row;row.key=id;
        if(plan.source_intent)row.source=*plan.source_intent;
        else if(plan.package.source==SourceKind::Official && !plan.package.appearance && !plan.payload)
            row.source=BrowseNpc{plan.package.npc_id};
        else throw std::runtime_error("layout source identity unavailable for character "+std::to_string(id));
        row.placement=status.placement;row.look=status.look;row.enabled=status.enabled;
        row.presentation_phase=status.presentation_phase;
        row.visual_state=registry.CaptureVisualProgram(id);
        if(const auto& action=registry.DesiredAction(id)) {
            if(const auto* npc=std::get_if<NpcMotion>(&action->request))row.action=*npc;
            else if(const auto* program=std::get_if<ProgramAction>(&action->request))row.action=*program;
            else if(std::holds_alternative<BattleIdle>(action->request))row.action=BattleIdle{};
            else if(const auto* configured=std::get_if<FreeStyleState>(&action->request))
                row.action=SavedConfiguredAction{action->domain,configured->name,configured->controller_observed};
            else if(const auto* animator=std::get_if<AnimatorState>(&action->request)) {
                Need(animator->layer==0,"independent controller layers are not persistent configured actions");
                row.action=SavedConfiguredAction{action->domain,animator->name};
            }
            else if(const auto* ready=std::get_if<ReadyState>(&action->request)) {
                Need(ready->actions && ready->pose<ready->actions->poses.size(),"ready source unavailable");
                const auto& pose=ready->actions->poses[ready->pose];
                row.action=SavedReadyAction{ready->actions->source,pose.state,pose.skill_name,pose.variant_index};
            } else throw std::runtime_error("development-only action cannot be saved for character "+std::to_string(id));
        }
        result.characters.push_back(std::move(row));
    }
    ValidateSceneLayout(result);return result;
}
std::optional<PlacedAction> RestoreLayoutAction(const std::optional<SavedAction>& action,const ActionCatalogStatus& catalog) {
    if(!action)return {};
    return std::visit([&](const auto& value)->PlacedAction {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,NpcMotion>)return {ActionDomain::Exploration,value};
        else if constexpr(std::is_same_v<T,ProgramAction>)return {ActionDomain::Alternate,value};
        else if constexpr(std::is_same_v<T,BattleIdle>)return {ActionDomain::Alternate,value};
        else if constexpr(std::is_same_v<T,SavedConfiguredAction>) {
            // This identity came from an observed official successor, not the
            // selectable config catalogue. Recheck membership at playback on
            // the newly loaded Animator; never persist numeric runtime hashes.
            if(value.controller_observed)return {value.domain,FreeStyleState{value.state,0,0,{},true}};
            if(!catalog.Ready(value.domain))throw std::runtime_error("action data is still being read");
            for(const auto& candidate:catalog.proposals)
                if(candidate.domain==value.domain && candidate.state==value.state)return {candidate.domain,candidate.Request()};
            throw std::runtime_error("saved configured action is absent: "+value.state);
        } else {
            if(!catalog.Ready(ActionDomain::Alternate))throw std::runtime_error("action data is still being read");
            std::optional<ReadyState> found;
            for(const auto& set:catalog.ready_actions)if(set->source==value.source)
                for(std::size_t i=0;i<set->poses.size();++i) {
                    const auto& pose=set->poses[i];
                    if(pose.state!=value.state || pose.skill_name!=value.skill || pose.variant_index!=value.variant)continue;
                    if(found)throw std::runtime_error("saved ready action is ambiguous: "+value.state);
                    found=ReadyState{set,i};
                }
            if(!found)throw std::runtime_error("saved ready action is absent: "+value.state);
            return {ActionDomain::Alternate,*found};
        }
    },*action);
}
void ValidateSceneLayout(const SceneLayout& layout) {
    Need(!layout.scene.empty() && layout.scene.find('\0')==std::string::npos,"layout needs a map identity");
    std::set<std::uint64_t> keys;
    for(const auto& row:layout.characters) {
        Need(row.key && keys.insert(row.key).second,"duplicate or zero layout character key");
        Need(!std::holds_alternative<CharacterPackage>(row.source),"external package layouts are not supported yet");
        Need(row.presentation_phase.find('\0')==std::string::npos,"invalid saved presentation phase");
        if(row.visual_state)ValidateVisualSnapshot(*row.visual_state);
        if(const auto* npc=std::get_if<BrowseNpc>(&row.source))Need(npc->id!=0,"NPC source ID must be nonzero");
        if(const auto* player=std::get_if<BrowsePlayer>(&row.source))Need(player->key.id!=0 && (!player->npc || *player->npc),"player source ID must be nonzero");
        Need(Finite(row.placement.position) && Finite(row.look.point),"nonfinite layout coordinate");
        Need(std::isfinite(row.look.max_distance) && row.look.max_distance>=0,"invalid saved gaze distance");
        const auto q=row.placement.rotation;
        const double norm=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
        Need(std::isfinite(norm) && std::abs(norm-1)<0.001,"layout rotation must be a unit quaternion");
        Need(row.look.mode==LookMode::Automatic || row.look.mode==LookMode::Disabled ||
            row.look.mode==LookMode::Player || row.look.mode==LookMode::Instance || row.look.mode==LookMode::Point,"invalid gaze mode");
        if(row.action)std::visit([](const auto& action){
            using T=std::decay_t<decltype(action)>;
            if constexpr(std::is_same_v<T,NpcMotion>)Need(action.id!=0,"NPC motion ID must be nonzero");
            else if constexpr(std::is_same_v<T,BattleIdle>){}
            else if constexpr(std::is_same_v<T,ProgramAction>) {
                auto name=[](const auto& value){return !value.empty() && value.find('\0')==std::string::npos;};
                Need(name(action.program) && name(action.action),"invalid saved program action identity");
                for(const auto& [key,value]:action.choices)Need(name(key) && name(value),"invalid saved program choice");
            }
            else {
                Need(!action.state.empty() && action.state.find('\0')==std::string::npos,"saved action state is empty or invalid");
                if constexpr(std::is_same_v<T,SavedReadyAction>)
                    Need(!action.source.empty() && action.source.find('\0')==std::string::npos && action.variant>=-1,"invalid ready action identity");
            }
        },*row.action);
    }
    for(const auto& row:layout.characters)if(row.look.mode==LookMode::Instance)
        Need(row.look.target!=row.key && keys.count(row.look.target)!=0,"gaze target must be another character in this map layout");
    for(const auto& row:layout.characters)if(row.visual_state) {
        auto check=[&](const auto& context){for(const auto& [name,id]:context.inputs.targets)
            Need(keys.count(id)!=0,"visual target must be a character in this map layout");};
        if(row.visual_state->scene)check(*row.visual_state->scene);
        for(const auto& context:row.visual_state->contexts)check(context);
    }
}
bool SameLayout(const SceneLayout& a,const SceneLayout& b) {
    if(a.scene!=b.scene || a.characters.size()!=b.characters.size())return false;
    auto position=[](auto x,auto y){return x.x==y.x && x.y==y.y && x.z==y.z;};
    for(std::size_t i=0;i<a.characters.size();++i) {
        const auto& x=a.characters[i];const auto& y=b.characters[i];
        if(bool(x.visual_state)!=bool(y.visual_state) || (x.visual_state && !SameVisualSnapshot(*x.visual_state,*y.visual_state)))return false;
        if(x.key!=y.key || x.enabled!=y.enabled || x.placement.snap_to_ground!=y.placement.snap_to_ground || x.look.max_distance!=y.look.max_distance || x.presentation_phase!=y.presentation_phase || x.source.index()!=y.source.index() || x.look.mode!=y.look.mode ||
            x.look.target!=y.look.target || !position(x.look.point,y.look.point) || !position(x.placement.position,y.placement.position))return false;
        const auto p=x.placement.rotation,q=y.placement.rotation;
        if(p.x!=q.x || p.y!=q.y || p.z!=q.z || p.w!=q.w)return false;
        if(const auto* n=std::get_if<BrowseNpc>(&x.source)) {if(n->id!=std::get<BrowseNpc>(y.source).id)return false;}
        else if(const auto* s=std::get_if<BrowsePlayer>(&x.source)) {
            const auto& t=std::get<BrowsePlayer>(y.source);
            if(std::tie(s->key.id,s->key.enhanced,s->key.skin,s->npc)!=std::tie(t.key.id,t.key.enhanced,t.key.skin,t.npc))return false;
        }else return false; // unsupported authored sources are never silently considered saved
        if(x.action.has_value()!=y.action.has_value())return false;
        if(x.action) {
            if(x.action->index()!=y.action->index())return false;
            const bool same=std::visit([&](const auto& left){
                using T=std::decay_t<decltype(left)>;const auto& right=std::get<T>(*y.action);
                if constexpr(std::is_same_v<T,NpcMotion>)return left.id==right.id;
                else if constexpr(std::is_same_v<T,BattleIdle>)return true;
                else if constexpr(std::is_same_v<T,SavedConfiguredAction>)return left.domain==right.domain && left.state==right.state && left.controller_observed==right.controller_observed;
                else if constexpr(std::is_same_v<T,ProgramAction>)return std::tie(left.program,left.action,left.choices)==std::tie(right.program,right.action,right.choices);
                else return std::tie(left.source,left.state,left.skill,left.variant)==std::tie(right.source,right.state,right.skill,right.variant);
            },*x.action);
            if(!same)return false;
        }
    }
    return true;
}
}
