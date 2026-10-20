#pragma once
#include "SceneCharacterState.hpp"
#include "CharacterSession.hpp"
#include "CharacterControls.hpp"
#include <hksr/entity/MapNpcInputs.hpp>
#include <hksr/presentation/PresentationContext.hpp>
#include <hksr/runtime/OperationTrace.hpp>
#include <map>

namespace hksr::framework {
// Saved user intent is separate from creation epochs. All methods execute on
// the official thread; UI only receives copied Status results. No registry
// destructor calls the game. Shutdown must drain sessions before unloading.
class SceneCharacters {
public:
    // Receives a session ALREADY owned by the registry. Prepare private SDK
    // creation inputs and call session.Create; even a callback exception after
    // Create leaves the partial epoch owned and serviced for cleanup.
    using Create=std::function<void(CharacterSession&,const api::WorldFrame&,const SceneCreation&)>;
    SceneCharacters(CharacterSessionServices services,Create create):services_(std::move(services)),create_(std::move(create)){}
    // Production bridge to the SDK's private NPC input factory.
    // No engine work occurs in the constructor.
    explicit SceneCharacters(CharacterSessionServices services);
    SceneCharacters(const SceneCharacters&)=delete;
    SceneCharacters& operator=(const SceneCharacters&)=delete;
    std::uint64_t Add(std::string scene,CharacterPlan plan,std::optional<PlacedAction> action={},CharacterPlacement placement={});
    std::uint64_t Select(std::uint64_t id,PlacedAction action);
    // Transient developer control; never rewrites the saved layout intention.
    TaskControlResult ControlTask(std::uint64_t id,std::uint64_t creation,TaskControlInput,const api::WorldFrame&);
    void SetEnabled(std::uint64_t id,bool enabled); // explicit suspension, not deletion
    void Delete(std::uint64_t id); // forget intent only after owned resources drain
    bool Contains(std::uint64_t id) const {return entries_.count(id)!=0;}
    const std::optional<PlacedAction>& DesiredAction(std::uint64_t id) const {return entries_.at(id).action;}
    void Rebuild(std::uint64_t id); // explicit retry/new creation, not automatic failure retry
    // Keep the user instance, scene, pose, enable flag and look intent. Retire
    // its old entity through the normal lifecycle; never replay old-source actions.
    void ReplacePlan(std::uint64_t id,CharacterPlan plan);
    void ConfigureControls(api::PresentationContext::ControlFactory create,api::PresentationContext::PlayerResolver player) {
        create_controls_=std::move(create);player_=std::move(player);
    }
    void ConfigureProfiles(std::shared_ptr<const PresentationProfiles> profiles) {profiles_=std::move(profiles);}
    void ConfigurePrograms(std::map<std::string,std::shared_ptr<const VisualProgramInput>> programs) {programs_=std::move(programs);}
    void ConfigureTiming(api::OperationTrace::Sink sink){timing_=std::move(sink);}
    void EnableTiming(bool enabled){timing_enabled_=enabled;}
    std::shared_ptr<const ControlSubmission> Place(std::uint64_t id,CharacterPlacement pose,bool relative);
    std::shared_ptr<const ControlSubmission> Look(std::uint64_t id,LookIntent intent);
    void Shutdown();
    void Service(const api::WorldFrame& frame,const std::optional<SceneAddress>& scene,
        AppearanceServices appearance,api::AnimationInputTypes types);
    SceneCharacterStatus Status(std::uint64_t id) const;
    std::vector<std::uint64_t> Ids() const;
    // Borrowed only for this official-thread call; never publish to UI.
    const CharacterSession* Active(std::uint64_t id,const api::WorldFrame& frame) const;
    const CharacterPlan& Plan(std::uint64_t id) const {return entries_.at(id).plan;}
    std::optional<VisualProgramSnapshot> CaptureVisualProgram(std::uint64_t id) const;
    void RestoreVisualProgram(std::uint64_t id,VisualProgramSnapshot);
    void CheckVisualRestore(CharacterPlan,const VisualProgramSnapshot&) const;
    bool Closed() const;
    std::size_t PendingInputReleases() const { return creation_inputs_.size(); }
private:
    struct Epoch {
        std::unique_ptr<CharacterSession> session;
        api::GameWorld* world=nullptr;
        std::uint64_t incarnation=0,revision=0;
        std::unique_ptr<CharacterControls> controls;
        std::uint64_t action_intent=0,session_action=0;
        std::uint64_t creation=0;
    };
    struct Entry {
        CharacterPlan plan;
        CharacterPlacement placement;
        std::optional<PlacedAction> action;
        SceneCharacterStatus status;
        std::uint64_t revision=1;
        std::uint64_t failed_revision=0;
        std::string creation_error;
        std::uint64_t next_action=0,action_intent=0;
        std::string action_error;
        std::string visual_restore_error;
        api::GameWorld* attempted_world=nullptr;
        std::uint64_t attempted_incarnation=0,attempted_revision=0;
        std::optional<Epoch> active;
        std::vector<Epoch> retired;
        LookIntent look;
        std::shared_ptr<ControlSubmission> look_submission;
    };
    void Retire(Entry& entry);
    void SaveVisualState(Entry& entry);
    Entry& Edit(std::uint64_t id);
    static bool LiveEntities(const Epoch& epoch);
    CharacterSessionServices services_;
    Create create_;
    struct CreationInput {std::uint64_t character;std::unique_ptr<api::MapNpcInputs> value;};
    std::vector<CreationInput> creation_inputs_;
    std::map<std::uint64_t,Entry> entries_;
    api::OperationTrace::Sink timing_;
    bool timing_enabled_=false;
    std::uint64_t next_=0;
    bool closing_=false,servicing_=false;
    api::PresentationContext::ControlFactory create_controls_;
    api::PresentationContext::PlayerResolver player_;
    std::shared_ptr<const PresentationProfiles> profiles_;
    std::map<std::string,std::shared_ptr<const VisualProgramInput>> programs_;
    void BindProfile(CharacterPlan& plan,std::uint64_t id) const;
    api::EntityOrigin VisualTarget(std::uint64_t id) const;
    void SubmitAction(Entry&);
};
}
