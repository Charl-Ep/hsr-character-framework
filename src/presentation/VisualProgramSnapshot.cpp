#include <hksr/framework/presentation/VisualProgram.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <stdexcept>

namespace hksr::framework {
void ValidateVisualSnapshot(const VisualProgramSnapshot& value) {
    auto name=[](const std::string& text){return !text.empty() && text.find('\0')==std::string::npos;};
    auto need=[](bool condition){if(!condition)throw std::invalid_argument("invalid visual snapshot value");};
    need(name(value.program));std::set<std::string> names;
    auto context=[&](const VisualContextSnapshot& item) {
        for(const auto& [key,id]:item.inputs.targets)need(name(key) && id);
        for(const auto& [key,choice]:item.inputs.choices)need(name(key) && name(choice));
        for(const auto& [key,number]:item.variables)need(name(key) && std::isfinite(number));
        for(const auto& [key,pose]:item.inputs.anchors) {
            const auto& p=pose.position;const auto& q=pose.rotation;
            const double norm=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
            need(name(key) && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(norm) && norm>0);
        }
    };
    if(value.scene){need(value.scene->name.empty());context(*value.scene);}
    for(const auto& item:value.contexts){need(name(item.name) && names.insert(item.name).second);context(item);}
}
bool SameVisualSnapshot(const VisualProgramSnapshot& a,const VisualProgramSnapshot& b) {
    if(a.program!=b.program || bool(a.scene)!=bool(b.scene) || a.contexts.size()!=b.contexts.size())return false;
    auto same=[](const VisualContextSnapshot& x,const VisualContextSnapshot& y) {
        if(std::tie(x.name,x.variables,x.inputs.targets,x.inputs.choices)!=std::tie(y.name,y.variables,y.inputs.targets,y.inputs.choices) ||
            x.inputs.anchors.size()!=y.inputs.anchors.size())return false;
        for(const auto& [key,pose]:x.inputs.anchors) {
            const auto found=y.inputs.anchors.find(key);if(found==y.inputs.anchors.end())return false;
            const auto& other=found->second;const auto& p=pose.position;const auto& q=pose.rotation;
            const auto& r=other.position;const auto& s=other.rotation;
            if(std::tie(p.x,p.y,p.z,q.x,q.y,q.z,q.w)!=std::tie(r.x,r.y,r.z,s.x,s.y,s.z,s.w))return false;
        }
        return true;
    };
    if(a.scene && !same(*a.scene,*b.scene))return false;
    for(std::size_t i=0;i<a.contexts.size();++i)if(!same(a.contexts[i],b.contexts[i]))return false;
    return true;
}
void RemapVisualTargets(VisualProgramSnapshot& value,const std::map<std::uint64_t,std::uint64_t>& keys) {
    auto remap=[&](auto& context){for(auto& [name,id]:context.inputs.targets)id=keys.at(id);};
    if(value.scene)remap(*value.scene);
    for(auto& context:value.contexts)remap(context);
}
VisualProgramSnapshot VisualProgramPlayer::Capture() const {
    if(status_.closing || end_pending_)return {program_->id,{},{}};
    if(restore_)return restore_->value;
    VisualProgramSnapshot out;out.program=program_->id;
    auto capture=[&](const auto& effects,const auto& choices,const auto& variables,const std::string& name) {
        VisualContextSnapshot value;value.name=name;value.variables=variables;
        if(services_.capture_inputs)value.inputs=services_.capture_inputs(*effects);
        value.inputs.choices=choices;return value;
    };
    if(context_) {
        if(!program_->restore)throw std::runtime_error("visual scene has no restore recipe: "+program_->id);
        out.scene=capture(context_,choices_,variables_,{});
    }
    std::vector<std::shared_ptr<Lifetime>> ordered;
    for(const auto& [name,lifetime]:contexts_)ordered.push_back(lifetime);
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a->token<b->token;});
    for(const auto& lifetime:ordered) {
        if(lifetime->phase!=ContextPhase::Active)throw std::runtime_error("visual context is changing: "+lifetime->name);
        if(!program_->contexts.at(lifetime->name).restore)
            throw std::runtime_error("visual context has no restore recipe: "+lifetime->name);
        out.contexts.push_back(capture(lifetime->effects,lifetime->choices,lifetime->variables,lifetime->name));
    }
    return out;
}
void ValidateVisualRestore(const VisualProgram& program,const VisualProgramSnapshot& value) {
    ValidateVisualSnapshot(value);
    if(value.program!=program.id)throw std::invalid_argument("snapshot belongs to another visual program");
    if(value.scene && (!value.scene->name.empty() || !program.restore))
        throw std::invalid_argument("visual scene restore recipe unavailable");
    std::set<std::string> names;
    for(const auto& context:value.contexts) {
        const auto found=program.contexts.find(context.name);
        if(!names.insert(context.name).second || found==program.contexts.end() || !found->second.restore)
            throw std::invalid_argument("visual context restore recipe unavailable or repeated: "+context.name);
    }
}
void VisualProgramPlayer::Restore(VisualProgramSnapshot value) {
    if(status_.closing || context_ || !contexts_.empty() || restore_ || pending_ || action_)
        throw std::logic_error("visual restoration requires an unentered scene");
    ValidateVisualRestore(*program_,value);
    restore_=Restoration{std::move(value),false,{}};
}
bool VisualProgramPlayer::ServiceRestore() {
    if(!restore_)return true;
    if(!status_.issues.empty())return false;
    // Native providers may queue Select/Leave/End. The restoration value and
    // cursor must outlive a reentrant cancellation; never dereference reset state.
    const auto snapshot=restore_->value;
    const auto intent=next_;
    auto ready=[&](const VisualContextSnapshot& saved) {
        if(!services_.restore_inputs_ready)return true;
        const auto result=services_.restore_inputs_ready(saved.inputs);
        if(result.state==VisualStepState::Failed)throw std::runtime_error(result.detail);
        if(result.state==VisualStepState::Pending)status_.waiting.push_back(result.detail);
        return result.state==VisualStepState::Done;
    };
    bool done=true;
    // Reserve all phase owners in original order before any resource waits.
    // Otherwise a slower early context could incorrectly override a later one.
    for(const auto& saved:snapshot.contexts)if(!contexts_.count(saved.name)) {
        try {StartContext(saved.name,&saved);}
        catch(const std::exception& e){status_.issues.push_back(std::string("visual restore: ")+e.what());done=false;}
        if(!restore_ || status_.closing || end_pending_)return false;
    }
    if(snapshot.scene && !restore_->scene_started) {
        try {
            if(ready(*snapshot.scene)) {
                if(!restore_ || status_.closing || end_pending_)return false;
                restore_->scene_started=true;
                context_=std::make_shared<VisualEffectContext>(services_.effects);
                variables_=snapshot.scene->variables;choices_=snapshot.scene->inputs.choices;
                if(services_.prepare_inputs)services_.prepare_inputs(context_,snapshot.scene->inputs,true);
                if(!restore_ || status_.closing || end_pending_)return false;
                if(services_.attach)services_.attach(context_);
                if(!restore_ || status_.closing || end_pending_)return false;
            }else done=false;
        }catch(const std::exception& e){status_.issues.push_back(std::string("visual restore: ")+e.what());done=false;}
    }
    if(context_)context_->Service();
    if(!restore_ || status_.closing || end_pending_)return false;
    for(const auto& saved:snapshot.contexts) {
        const auto found=contexts_.find(saved.name);
        if(found==contexts_.end()){done=false;continue;}
        auto lifetime=found->second;
        try {
            if(lifetime->phase!=ContextPhase::Active && ready(saved))DriveContext(lifetime);
        }catch(const std::exception& e){status_.issues.push_back(saved.name+": "+e.what());}
        if(!restore_ || status_.closing || end_pending_)return false;
    }
    if(!restore_ || next_!=intent)return false;
    for(const auto& [name,lifetime]:contexts_)done=done && lifetime->phase==ContextPhase::Active;
    if(snapshot.scene && restore_->scene_started) {
        auto cursor=restore_->scene;
        done=Instructions(*program_->restore,cursor,{intent,{}}) && done;
        if(!restore_)return false;
        restore_->scene=std::move(cursor);
    }
    if(!done || !status_.issues.empty() || next_!=intent)return false;
    if(snapshot.scene)restored_inputs_=snapshot.scene->inputs;
    restore_.reset();return true;
}
}
