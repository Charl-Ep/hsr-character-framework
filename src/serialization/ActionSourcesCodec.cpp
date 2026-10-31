#include <hksr/framework/catalog/ActionSourceStore.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

// File-boundary serializers only; the catalog and its consumers use SDK values.
namespace hksr::sdk_v1 {
using nlohmann::json;
template<class T> void to_json(json& j,const ConfigValue<T>& v){j={{"available",v.available},{"value",v.value},{"error",v.error}};}
template<class T> void from_json(const json& j,ConfigValue<T>& v){j.at("available").get_to(v.available);j.at("value").get_to(v.value);j.at("error").get_to(v.error);}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TextInput,available,value,error)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TextArrayInput,available,values,error)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CatalogIssue,index,operation,error)
template<class T> void to_json(json& j,const CatalogBatch<T>& v){j={{"rows",v.rows},{"declared_count",v.declared_count},{"visited_count",v.visited_count},{"enumerated",v.enumerated},{"issues",v.issues}};}
template<class T> void from_json(const json& j,CatalogBatch<T>& v){j.at("rows").get_to(v.rows);j.at("declared_count").get_to(v.declared_count);j.at("visited_count").get_to(v.visited_count);j.at("enumerated").get_to(v.enumerated);j.at("issues").get_to(v.issues);}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PresentationConfigInput,path,initial_state,event_files,disabled_event_layers)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConfigReference,value,field)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SkillReadyVariant,source_index,state,has_condition,priority)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SkillInput,source_index,name,ready_state,entry_ability,prepare_ability,ready_effect,type,use_transition,cut_1_to_2,cut_2_to_1,wait_prepare,auto_standby,ready_wait,child_skills,variants)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ReadyTransitionInput,source_index,from,to,state,transition_in,reverse_outro)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ElationReadyInput,source_index,skill,entry,loop)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ActionConfigInput,kind,path,loaded,presentation,referenced_event_file,skills,transitions,states,abilities,issues,elation_config,elation_ready)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConfigStep,declaring_type,field,selection,index,key)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AnimationTaskSource,ability,path,kind,state,logic_kind,enabled,waits,target_alias)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AnimationWaitSource,ability,path,state,target_alias,enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EffectRemovalInput,effect_path,path_key,target_alias,enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EffectRemovalSource,ability,path,input)
void to_json(json& j,const AbilitySourceIssue& v){j={{"ability",v.ability},{"error",v.error},{"path",v.path},{"kind",v.kind}};}
void from_json(const json& j,AbilitySourceIssue& v){
    j.at("ability").get_to(v.ability);j.at("error").get_to(v.error);j.at("path").get_to(v.path);
    // Unclassified diagnostics remain read failures. Omission cannot promote
    // an old unknown error into a supported/nonliteral observation.
    v.kind=j.value("kind",AbilitySourceIssueKind::ReadFailure);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AbilityReferenceSource,ability,path,target,enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SkillMappingSource,ability,path,skill,target_alias,control,enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SkillContinuationSource,ability,path,control,child,enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AbilitySources,animations,issues,nodes_read,waits,removals,references,skill_mappings,skill_continuations)
}
namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
void CheckPath(const std::vector<api::ConfigStep>& path) {
    for(const auto& step:path)Need(step.selection>=api::ConfigSelection::Reference && step.selection<=api::ConfigSelection::StringKey,"invalid config path selection");
}
void CheckAbility(const std::string& name,const api::AbilitySources& value) {
    auto rows=[&](const auto& entries){for(const auto& row:entries){Need(row.ability==name,"ability record owner mismatch");CheckPath(row.path);}};
    rows(value.animations);rows(value.waits);rows(value.removals);rows(value.references);rows(value.skill_mappings);rows(value.skill_continuations);
    for(const auto& row:value.animations)Need(row.kind>=api::AnimationTaskKind::State && row.kind<=api::AnimationTaskKind::StateWithTargetList,"invalid animation task kind");
    for(const auto& issue:value.issues)Need(issue.kind>=api::AbilitySourceIssueKind::ReadFailure &&
        issue.kind<=api::AbilitySourceIssueKind::Unsupported,"invalid source diagnostic kind");
}
}
std::string EncodeActionSources(const ActionSourceData& value) {
    Json out={{"format","hksr.action-sources"},{"schema",1},{"reader_contract",1},
        {"identity",{{"build",value.identity.build},{"design",value.identity.design},{"markers",value.identity.markers}}},
        {"documents",Json::array()},{"abilities",Json::object()},
        {"invalid_documents",Json::array()},{"invalid_abilities",value.invalid_abilities}};
    for(const auto& key:value.invalid_documents)out["invalid_documents"].push_back({{"kind",key.kind},{"path",key.path}});
    for(const auto& row:value.documents) {
        out["documents"].push_back(*row.second);
    }
    for(const auto& row:value.abilities) {
        out["abilities"][row.first]=*row.second;
    }
    return out.dump(2)+"\n";
}
ActionSourceData DecodeActionSources(std::string_view text) {
    const auto in=Json::parse(text.begin(),text.end());
    Need(in.at("format")=="hksr.action-sources" && in.at("schema")==1 && in.at("reader_contract")==1,"unsupported action source format/reader contract");
    ActionSourceData out;const auto& identity=in.at("identity");
    identity.at("build").get_to(out.identity.build);identity.at("design").get_to(out.identity.design);identity.at("markers").get_to(out.identity.markers);
    Need(!out.identity.build.empty() && out.identity.design && !out.identity.markers.empty(),"action source identity incomplete");
    Need(in.at("documents").is_array() && in.at("abilities").is_object(),"invalid action source collections");
    for(const auto& key:in.at("invalid_documents"))out.invalid_documents.insert({key.at("kind").get<api::ActionConfigKind>(),key.at("path").get<std::string>()});
    in.at("invalid_abilities").get_to(out.invalid_abilities);
    for(const auto& data:in.at("documents")) {
        auto value=data.get<api::ActionConfigInput>();
        Need(!value.path.empty(),"document path absent in action source file");
        Need(value.kind>=api::ActionConfigKind::Adventure && value.kind<=api::ActionConfigKind::Elation,"invalid action document kind");
        const ActionSourceKey key{value.kind,value.path};
        Need(out.documents.emplace(key,std::make_shared<const api::ActionConfigInput>(std::move(value))).second,"duplicate action source document");
    }
    for(auto it=in.at("abilities").begin();it!=in.at("abilities").end();++it) {
        Need(!it.key().empty(),"ability name absent in action source file");
        auto value=it.value().get<api::AbilitySources>();CheckAbility(it.key(),value);
        out.abilities.emplace(it.key(),std::make_shared<const api::AbilitySources>(std::move(value)));
    }
    return out;
}
}
