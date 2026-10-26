#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
namespace {
template<class T>const T& Value(const api::ConfigValue<T>& value,const char* name) {
    if(!value.available)throw std::runtime_error(std::string("receiver input unread: ")+name+" "+value.error);
    return value.value;
}
const std::string& Value(const api::TextInput& value,const char* name) {
    if(!value.available)throw std::runtime_error(std::string("receiver input unread: ")+name+" "+value.error);
    return value.value;
}
template<class T>std::optional<T> Optional(const api::ConfigValue<T>& value) {
    return value.available?std::optional<T>(value.value):std::nullopt;
}
}
HitResolution ActionPlayback::ResolveIncomingHit(const HitInput& input,const api::WorldFrame& frame,api::ManagedObjects& objects) {
    resolved_hit_.reset();
    if(input.animation.empty())return {true,{}};
    if(!hit_catalog_ || !hit_control_)throw std::runtime_error("receiver hit policy is not configured");
    if(!presentation_enabled_)throw std::runtime_error("receiver is not in battle presentation domain");
    const auto revision=intent_revision_;
    auto live=[&]{return !Ended() && !status_.closing && revision==intent_revision_ && OwnFrame(frame);};
    const auto control=hit_control_(true);
    const auto phase=program_?program_->CurrentPhase():nullptr;
    const auto catalog=hit_catalog_(control.visual_flag.has_value() && !phase);
    if(!catalog || !live())return {};
    if(!catalog->character || !catalog->names || !catalog->weights || !catalog->animations)
        throw std::runtime_error("receiver catalog is incomplete");
    const auto& character=*catalog->character;
    const bool base_phase=phase?false:Value(character.phase_present,"PhaseAnimConfig");
    const auto* reactions=phase?&phase->reactions:base_phase?&character.phase:nullptr;
    std::optional<VisualState> visual;
    if(control.visual_flag) {
        const auto* table=phase?&phase->visuals:catalog->visuals.get();
        if(!table || !table->context_available)throw std::runtime_error("receiver active visual table unread");
        visual=ResolveVisualState(*table,*control.visual_flag);
    }
    // Direct reaction previews also have a real retained row, independently of
    // task-control sources. An unrelated manual body action clears this owner.
    if(!control.visual_flag && visual_ && !visual_->request.recover)visual=visual_->request;
    resources_.RetainOwner();if(!live())return {};
    const auto component=resources_.Component(api::AnimationComponent::Model,objects);if(!live())return {};
    if(!component.call.returned || !component.object)throw std::runtime_error("receiver model unread");
    const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));if(!live())return {};
    if(!model.animator.read.returned || !model.changing.read.returned)throw std::runtime_error("receiver Animator unread");
    if(model.changing.value || !model.animator.value)return {};
    const auto initialized=inputs_.AnimatorInitialized(model.animator.value);if(!live())return {};
    if(!initialized.read.returned)throw std::runtime_error("receiver Animator initialization unread");
    if(!initialized.value)return {};
    if(hit_policy_animator_!=model.animator.value){hit_policy_animator_=model.animator.value;previous_hit_.reset();received_hit_.reset();}

    HitReceiverState receiver;receiver.frame=frame.sequence;
    receiver.endurance=control.endurance;receiver.mute_heavy=control.mute_heavy;receiver.force_heavy=control.force_heavy;
    receiver.resilience=Optional(character.resilience);
    receiver.repeat_occur_at=Optional(character.repeat_occur_at);
    const auto kind=entities_.Type(owner_.entity);if(!live())return {};
    // ca12320 uses a multiplier only for Avatar/Monster; independent story
    // actors retain the native other-entity multiplier of one.
    receiver.entity_weight=!kind.read.returned?std::nullopt:kind.value==1?Optional(catalog->weights->avatar_weight):
        kind.value==2?Optional(catalog->weights->monster_weight):std::optional<float>(1.f);
    const auto motion=resources_.Component(api::AnimationComponent::Motion,objects);if(!live())return {};
    if(!motion.call.returned)throw std::runtime_error("receiver motion lookup failed");
    if(motion.object) {
        const auto height=actions_.HitMotionHeight(motion.object);if(!live())return {};
        if(!height.read.returned || !std::isfinite(height.value))throw std::runtime_error("receiver motion height unread");
        receiver.height=height.value;
    } // Successful null lookup: no native flight/fall producer, not world Y=0.
    // HitInput currently carries no hit-motion request. Do not synthesize one
    // from the animation name, or borrow a previous attack's launch parameters.
    receiver.has_launch_motion=false;receiver.launch_allowed=false;
    if(visual) {
        const auto& occur=visual->value.occur.state;
        if(!occur.available)receiver.in_occur.reset();
        else if(!occur.value.empty()) {
            const auto hash=HashName(occur.value,objects);if(!live())return {};
            const auto state=actions_.CurrentState(model.animator.value,0);if(!live())return {};
            if(state.read.returned) {
                receiver.in_occur=state.short_name_hash==hash || state.full_path_hash==hash;
                receiver.occur_progress=state.normalized_time;
            }else {receiver.in_occur.reset();receiver.occur_progress.reset();}
        }
    }
    const auto decision=SelectHit(input.animation,receiver,{*catalog->names,reactions,visual?&visual->value:nullptr},
        *catalog->weights,previous_hit_);
    if(decision.kind==HitKind::None || decision.throttled)return {true,{}};
    HitPlaybackDefaults names;
    names.additive_layer=Value(catalog->animations->hit_additive_layer,"HitAdditiveLayer");
    names.override_layer=Value(catalog->animations->hit_override_layer,"HitOverrideLayer");
    // This player and its FreeStyle body endpoint own layer zero. Obtain that
    // actual model's name, rather than a guessed name or CFF from another actor.
    if(decision.kind==HitKind::Heavy || decision.kind==HitKind::Launch) {
        if(!text_)throw std::runtime_error("receiver layer text reader absent");
        const auto name=resources_.LayerName(model.animator.value,0,actions_,*text_);if(!live())return {};
        if(!name.read.returned)throw std::runtime_error("receiver body layer text unread");
        names.visual_layer=name.utf8;
    }
    // Native Hit does not replace an active visual expression (ca1094e..95a).
    if(!visual || Value(visual->expression,"visual expression").empty())
        names.expression=Value(catalog->animations->hit_expression,"hit expression");
    names.expression_transition=.2f;names.blink_source=2; // qualified ca108f0 / OnHit blink source
    // OnHit's additive submit uses normalized transition zero. Direction-based
    // start offsets and the body request's logical track remain separate work.
    auto command=BuildHitCommand(decision,names);
    // GOK's ordinary OnHit request passes End=0. It does not ask for a
    // normalized-time-one return; the controller's destination is checked later.
    if(command.body)command.body->normalized_end=0.f;
    command.expression_seconds=.5f; // ordinary OnHit expression lifetime, independent of body return
    command.receiver_return=true;
    if(!live())return {};
    resolved_hit_=ResolvedHit{decision,frame.sequence};return {true,std::move(command)};
}
std::optional<HitReturn> ActionPlayback::ResolveHitReturn(bool body,const api::WorldFrame& frame,api::ManagedObjects& objects,bool require_grounded) {
    if(!hit_catalog_ || !hit_control_ || !presentation_enabled_ || Ended() || status_.closing || !OwnFrame(frame))return {};
    const auto revision=intent_revision_;
    auto live=[&]{return !Ended() && !status_.closing && revision==intent_revision_ && OwnFrame(frame);};
    // ca108f0 restores from today's source/phase, not the expression captured
    // when the hit began. These shared caches do not enumerate action menus.
    const auto alive=entities_.AliveState(owner_.entity);if(!live())return {};
    if(!alive.read.returned)throw std::runtime_error("receiver return alive state unread");
    if(alive.value!=1)return {};
    const auto control=hit_control_(false);
    const auto phase=program_?program_->CurrentPhase():nullptr;
    const auto catalog=hit_catalog_(control.visual_flag.has_value() && !phase);
    if(!catalog || !live())return {};
    if(!catalog->animations)throw std::runtime_error("receiver return defaults unread");
    std::optional<VisualState> visual;
    if(control.visual_flag) {
        const auto* table=phase?&phase->visuals:catalog->visuals.get();
        if(!table || !table->context_available)throw std::runtime_error("receiver return visual table unread");
        visual=ResolveVisualState(*table,*control.visual_flag);
    }else if(visual_ && !visual_->task_owned && !visual_->request.recover)visual=visual_->request;
    HitReturn out;
    out.expression=visual?Value(visual->expression,"return visual expression"):std::string{};
    if(out.expression.empty())out.expression=Value(catalog->animations->default_expression,"default expression");
    out.expression_transition=visual?Value(visual->expression_transition,"return expression transition"):.2f;
    if(body) {
        if(require_grounded) {
            const auto motion=resources_.Component(api::AnimationComponent::Motion,objects);if(!live())return {};
            if(!motion.call.returned)throw std::runtime_error("receiver return motion lookup failed");
            if(motion.object) {
                const auto height=actions_.HitMotionHeight(motion.object);if(!live())return {};
                if(!height.read.returned || !std::isfinite(height.value))throw std::runtime_error("receiver return height unread");
                if(height.value>0)return {}; // ordinary hit return, not active-skill completion
            }
        }
        const auto idle=SelectIdle({true,CompletionState(),{}},catalog->animations->default_expression,phase.get(),
            visual?&visual->value:nullptr,visual?&visual->expression:nullptr);
        out.body=idle.state;
    }
    return live()?std::optional<HitReturn>(std::move(out)):std::nullopt;
}
}
