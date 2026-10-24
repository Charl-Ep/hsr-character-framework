#include <hksr/framework/host/FrameworkHost.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <stdexcept>
#include <type_traits>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace hksr::framework {
namespace {
class TimedAbilityRead final : public api::ProgressiveAbilitySourceRead {
    std::unique_ptr<api::AbilitySourceRead> read_;
    std::function<void(const std::string&)> trace_;
    std::size_t roots_,slices_=0;
    std::int64_t total_=0,max_=0;
    bool cancelled_=false,reported_=false;
public:
    TimedAbilityRead(std::unique_ptr<api::AbilitySourceRead> read,std::size_t roots,std::function<void(const std::string&)> trace)
        :read_(std::move(read)),trace_(std::move(trace)),roots_(roots){}
    void Advance(std::uint32_t budget) override {
        const auto start=std::chrono::steady_clock::now();read_->Advance(budget);
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        ++slices_;total_+=elapsed;max_=std::max<std::int64_t>(max_,elapsed);
        if(read_->Done() && !reported_) {
            reported_=true;
            trace_("ability discovery roots="+std::to_string(roots_)+" slices="+std::to_string(slices_)+
                " total_us="+std::to_string(total_)+" max_us="+std::to_string(max_)+" cancelled="+std::to_string(cancelled_));
        }
    }
    void Cancel() override {cancelled_=true;read_->Cancel();}
    bool Done() const override {return read_->Done();}
    api::AbilitySources Take() override {return read_->Take();}
    api::CompletedAbilitySources DrainCompleted() override {
        if(auto* progressive=dynamic_cast<api::ProgressiveAbilitySourceRead*>(read_.get()))return progressive->DrainCompleted();
        return {};
    }
};
}
void FrameworkHost::Assemble(api::ManagedObjects& objects,std::unique_ptr<RuntimeServices> services,api::RuntimeCapabilities capabilities) {
    if(services_) throw std::logic_error("framework host already prepared");
    objects_=&objects;capabilities_=capabilities;
    services_=std::move(services);
    identity_=std::make_unique<SceneIdentity>(services_->CreateRoots());
    roots_=services_->CreateRoots();
    binding_errors_=services_->BindingErrors();
    if(!capabilities.event_record_lifetime)binding_errors_.push_back("event lifetime observer unavailable; private event supplements not installed");
}
void FrameworkHost::Prepare(api::RuntimeMetadata& metadata,api::RuntimeCapabilities capabilities) {
    if(services_)throw std::logic_error("framework host already prepared");
    Assemble(metadata,std::make_unique<RuntimeServices>(metadata,capabilities,api::PresentationIsolationPolicy{},
        [this](const auto& text){Trace(text);}),capabilities);
    try {
        event_policy_=services_->CreateAnimationPolicy(api::AnimationTaskPolicy{});
    }catch(const std::exception& e){Trace(std::string("event configuration policy unavailable: ")+e.what());}
    official_catalog_=services_->CreateCatalog();
    catalog_metadata_=&metadata;
    if(task_development_ || !programs_.empty())services_->PrepareVisualPrograms([this](const auto& text){Trace(text);},
        [this,&metadata](const auto& character,const auto& source) {
            return Measure("catalog.visual-phase",[&]{return official_catalog_->ReadPhaseVisualConfigSources(character,source,metadata);});
        });
    auto sessions=services_->Sessions(capabilities.event_record_lifetime);
    sessions.pose=[this](const auto& roots,const auto& selection){return Browser().ResolvePose(roots,selection);};
    characters_=std::make_unique<SceneCharacters>(std::move(sessions));
    characters_->ConfigureProfiles(profiles_);
    characters_->ConfigurePrograms(programs_);
    characters_->ConfigureControls(services_->Controls(),services_->Player());
    characters_->ConfigureTiming([this](const api::OperationTraceReport& report) {
        if(report.rows.size()<2)return; // No operation ran, e.g. a deferred creation.
        for(std::size_t i=0;i<report.rows.size();++i) {
            const auto& row=report.rows[i];
            std::ostringstream text;
            text<<"operation-timing actor="<<report.subject<<" generation="<<report.generation
                <<" frame="<<report.frame<<" tick="<<report.tick_ms<<" source="<<report.source
                <<" row="<<i<<" parent="<<row.parent<<" stage="<<row.stage
                <<" target=0x"<<std::hex<<row.native_target<<" rva=0x"<<row.native_rva<<std::dec
                <<" module="<<std::quoted(row.native_module)<<" calls="<<row.calls
                <<" inclusive_ns="<<row.inclusive_ns<<" self_ns="<<row.self_ns
                <<" max_ns="<<row.max_ns<<" failures="<<row.failures<<" dropped="<<report.dropped;
            Trace(text.str());
        }
    });
    try {
        mouse_=services_->CreateMouseMode();
    }catch(const std::exception& e){Trace(std::string("mouse mode unavailable: ")+e.what());}
    CatalogReaders readers;
    readers.characters=[this]{return Measure("catalog.tables",[&]{return official_catalog_->ReadCharacters();});};
    readers.identity=[this](const auto& path){return Measure("catalog.identity:"+path,[&]{return official_catalog_->ReadAdventureIdentity(path);});};
    readers.document=[this](auto kind,const auto& path){return Measure("catalog.document:"+path,[&]{return official_catalog_->ReadActionConfig(kind,path);});};
    readers.npc_motions=[this](void* component){return Measure("catalog.npc-motions",[&]{return official_catalog_->ReadNpcMotionsFromComponent(component);});};
    readers.visuals=[this](const auto& path){return Measure("catalog.visual:"+path,[&]{return official_catalog_->ReadVisualConfigSources(path);});};
    readers.ready_defaults=[this]{return Measure("catalog.ready-defaults",[&]{return official_catalog_->ReadReadyAnimationDefaults();});};
    readers.begin_abilities=[this,&metadata](const auto& names)->std::unique_ptr<api::AbilitySourceRead>{
            return std::make_unique<TimedAbilityRead>(official_catalog_->BeginAbilitySources(names,metadata),names.size(),
                [this](const auto& message){Trace(message);});};
    auto browser=std::make_unique<CatalogBrowser>(std::move(readers),[this](const CatalogWork& work){
            Trace("catalog completed slices="+std::to_string(work.slices)+" total_us="+std::to_string(work.total_us)+
                " max_us="+std::to_string(work.max_us)+" source_steps="+std::to_string(work.source_steps)+
                " action_steps="+std::to_string(work.action_steps));
        });
    PrepareApplication(std::move(browser));
    application_.Post(SetCameraPresentations{false});
    ApplicationSnapshot snapshot;snapshot.prepared=true;snapshot.binding_errors=binding_errors_;application_.Publish(std::move(snapshot));
}
void FrameworkHost::Prepare(api::ManagedObjects& objects,api::PresentationBindings bindings,
    api::RuntimeCapabilities capabilities,SceneCharacters::Create create,CatalogReaders catalog) {
    Assemble(objects,std::make_unique<RuntimeServices>(objects,std::move(bindings)),capabilities);
    auto sessions=services_->Sessions(capabilities.event_record_lifetime);
    sessions.pose=[this](const auto& roots,const auto& selection){return Browser().ResolvePose(roots,selection);};
    characters_=std::make_unique<SceneCharacters>(std::move(sessions),std::move(create));
    PrepareApplication(catalog.characters?std::make_unique<CatalogBrowser>(std::move(catalog)):nullptr);
    ApplicationSnapshot snapshot;snapshot.prepared=true;snapshot.binding_errors=binding_errors_;application_.Publish(std::move(snapshot));
}
void FrameworkHost::PrepareApplication(std::unique_ptr<CatalogBrowser> catalog) {
    ApplicationRuntimePorts runtime;
    runtime.camera_presentations=[this](bool enabled){services_->SetCameraPresentations(enabled);};
    runtime.player_placement=[this](const api::WorldFrame& frame) {
        const auto player=services_->ReadPlayerPlacement(frame);
        if(!player.ready)throw std::runtime_error(player.error);
        return CharacterPlacement{player.position,player.rotation};
    };
    runtime.npc_actions=[this](auto id,const auto& frame){ReadNpcActions(id,frame);};
    runtime.controller_actions=[this](auto id){controller_query_=id;controller_stamps_.erase(id);};
    runtime.observe_character=[this](auto id){SetObservedCharacter(id);};
    runtime.source_replaced=[this](auto id){
        npc_actions_.erase(id);controller_actions_.erase(id);
        controller_stamps_.erase(id);controller_read_errors_.erase(id);
    };
    runtime.removed=[this](auto id){
        npc_actions_.erase(id);controller_actions_.erase(id);
        controller_stamps_.erase(id);controller_read_errors_.erase(id);
        last_character_trace_.erase(id);
        if(controller_query_ && *controller_query_==id)controller_query_.reset();
        if(observed_character_.load()==id)SetObservedCharacter(0);
    };
    coordinator_=std::make_unique<ApplicationCoordinator>(application_,*characters_,std::move(catalog),
        capabilities_,std::move(runtime),[this](const auto& message){Trace(message);});
    if(layout_io_)coordinator_->ConfigureLayouts(layout_io_);
}
void FrameworkHost::PrepareTypes() {
    if(types_attempted_) return;
    types_attempted_=true;
    // Runtime type objects are obtained/retained on the official frame, not
    // the bootstrap worker. Missing event types do not suppress plain NPCs.
    const auto input=api::ResolveAnimationInputTypes(*objects_);
    auto keep=[&](api::ObjectResult value) {
        if(!value.call.returned || !value.object) return value;
        const auto id=roots_->Keep(value.object);type_roots_.push_back(id);return roots_->Target(id);
    };
    types_.data=keep(input.data);types_.events=keep(input.events);
}
CatalogBrowser& FrameworkHost::Browser() {
    return coordinator_->RequireCatalog();
}
bool FrameworkHost::Frame(const api::WorldFrame& frame,bool stop_requested) {
    services_->ActivateIsolation(frame);
    if(event_policy_)event_policy_->ActivateOnCurrentThread();
    if(!characters_) throw std::logic_error("framework host not prepared");
    if(mouse_) {
        mouse_->Service(menu_mouse_.load() && !stop_requested && !closing_);
        const auto note=mouse_->Status();if(note!=mouse_trace_){mouse_trace_=note;Trace("mouse mode: "+note);}
    }
    if(stop_requested && !closing_) {coordinator_->CaptureLayouts(true);closing_=true;characters_->Shutdown();}
    auto batch=application_.TakeCommands(closing_);
    const auto scene=Measure("scene.observe",[&]{return closing_ ? std::optional<SceneAddress>{} : identity_->Observe(frame,services_->ReadMap(frame));});
    if(!closing_ && frame.world && frame.disposing.read.returned && !frame.disposing.value &&
        frame.battle.read.returned && !frame.battle.value)coordinator_->AdvanceLayouts(scene);
    for(auto& pending:batch)
        Measure("command."+std::to_string(pending.command.index()),[&]{coordinator_->Dispatch(pending,scene,frame,closing_);});
    if(!closing_ && !types_attempted_ && !characters_->Ids().empty()) {
        try {Measure("types.prepare",[&]{PrepareTypes();});}
        catch(const std::exception& error) {binding_errors_.push_back(error.what());}
    }
    characters_->EnableTiming(operation_tracing_.load());
    Measure("instances.service",[&]{characters_->Service(frame,scene,services_->Appearance(),types_);});
    if(!closing_)coordinator_->CaptureLayouts();
    Measure("isolation.collect",[&]{services_->CollectIsolation();});
    // Service owned instances before directory work. Queries remain synchronous
    // on this observed exploration frame; not Present/bootstrap threads.
    if(!closing_ && coordinator_->Catalog() && scene && frame.world && frame.disposing.read.returned && !frame.disposing.value &&
        frame.battle.read.returned && !frame.battle.value) {
        Measure("catalog.advance",[&]{coordinator_->Catalog()->Advance();});
        Measure("model.observe",[&]{ReadControllerActions(frame);});
    }
    if(closing_) {
        // Drain managed query roots on the official thread, not later from the
        // bootstrap worker's host destructor. No additional discovery on exit.
        coordinator_->CloseCatalog();
        identity_->Close();
        if(characters_->Closed()) {
            if(event_policy_)event_policy_->Stop();
            effects_closed_=services_->Close();
            for(auto it=type_roots_.begin();it!=type_roots_.end();) {
                if(roots_->Release(*it).complete) {roots_->Forget(*it);it=type_roots_.erase(it);}else ++it;
            }
        }
    }
    Measure("snapshot.publish",[&]{Publish(scene);});PublishPerformance();return application_.Snapshot().closed;
}
}
