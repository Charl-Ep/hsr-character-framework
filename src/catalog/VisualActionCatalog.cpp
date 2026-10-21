#include <hksr/framework/catalog/VisualActionCatalog.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
VisualActionAvailability QueryVisualAction(const VisualState& row,bool recover) {
    VisualActionAvailability out;
    auto issue=[&](std::string detail){out.available=false;out.issues.push_back(std::move(detail));};
    bool body=false;
    auto phase=[&](const api::VisualPhaseInput& value,const char* name) {
        if(!value.resolved_state.available){issue(std::string(name)+": "+value.resolved_state.error);return;}
        if(value.resolved_state.value.empty())return;
        body=true;
        if(!value.transition.available || !std::isfinite(value.transition.value) || value.transition.value<0)
            issue(std::string(name)+": transition unavailable");
    };
    if(recover)phase(row.value.recover,"recover");
    else {phase(row.value.occur,"occur");phase(row.value.loop,"loop");}
    if(!body)issue("no animation in this visual phase");
    if(!recover) {
        if(!row.expression.available)issue("expression: "+row.expression.error);
        else if(!row.expression.value.empty() && (!row.expression_transition.available ||
            !std::isfinite(row.expression_transition.value) || row.expression_transition.value<0))
            issue("expression transition unavailable");
    }
    return out;
}
bool VisualActionSourceReady(const ActionCatalogStatus& catalog,const std::string& path) {
    for(const auto& source:catalog.visual_sources)if(source.character_config==path)return true;
    return catalog.finished; // a completed catalog can report an absent source
}
VisualState ResolveVisualAction(const ActionCatalogStatus& catalog,const VisualActionKey& key) {
    const VisualState* result=nullptr;
    for(const auto& row:catalog.visual_states)
        if(row.character_config==key.character_config && row.value.flag.available && row.value.flag.value==key.flag) {
            if(result)throw std::runtime_error("ambiguous visual behavior identity");
            result=&row;
        }
    if(!result)throw std::runtime_error("visual behavior absent from this character's configuration");
    const auto availability=QueryVisualAction(*result,key.recover);
    if(!availability.available)throw std::runtime_error(availability.issues.front());
    auto value=*result;value.recover=key.recover;return value;
}
}
