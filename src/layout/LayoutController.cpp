#include <hksr/framework/layout/LayoutController.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace hksr::framework {
namespace {
bool Equal(const MapLayoutStatus& a, const MapLayoutStatus& b) {
    return std::tie(a.initialized, a.busy, a.phase, a.error, a.storage_error, a.operation, a.name, a.command,
                    a.revision, a.saved_revision, a.names) == std::tie(b.initialized, b.busy, b.phase, b.error,
                                                                       b.storage_error, b.operation, b.name, b.command,
                                                                       b.revision, b.saved_revision, b.names);
}
}
LayoutController::LayoutController(SceneCharacters& characters, CatalogBrowser& catalog, std::shared_ptr<LayoutIo> io,
                                   std::function<void(std::uint64_t)> removed, bool private_models)
    : characters_(characters), catalog_(catalog), io_(std::move(io)), removed_(std::move(removed)),
      private_models_(private_models) {}
void LayoutController::Release(Map& map) {
    catalog_.BatchSources([&] {
        for (auto id : map.sources)
            catalog_.ReleaseSource(id);
    });
    map.sources.clear();
    map.plans.clear();
    map.actions.clear();
    map.plan_ready.clear();
    map.action_views.clear();
}
void LayoutController::Fail(Map& map, std::string error) {
    Release(map);
    map.phase = RestorePhase::Failed;
    map.status.phase = LayoutPhase::Failed;
    map.status.busy = false;
    map.status.error = std::move(error);
}
void LayoutController::Begin(Map& map, SceneLayout layout) {
    ValidateSceneLayout(layout);
    Release(map);
    map.incoming = std::move(layout);
    map.phase = RestorePhase::Preparing;
    map.status.phase = LayoutPhase::Preparing;
    map.status.busy = true;
    map.status.error.clear();
    catalog_.BatchSources([&] {
        for (const auto& row : map.incoming->characters)
            map.sources.push_back(catalog_.Prepare(row.source));
    });
    const auto count = map.sources.size();
    map.plans.resize(count);
    map.actions.resize(count);
    map.plan_ready.resize(count, false);
    map.action_views.resize(count);
}
void LayoutController::Prepare(Map& map) {
    const auto snapshot = catalog_.Snapshot();
    bool pending = false;
    for (std::size_t i = 0; i < map.sources.size(); ++i) {
        const auto* source = snapshot->Source(map.sources[i]);
        if (!source)
            throw std::runtime_error("layout source request was released");
        if (source->Pending()) {
            pending = true;
            continue;
        }
        if (source->phase != SourceRequestPhase::Ready) {
            std::string error = "layout source unresolved (entry " + std::to_string(map.incoming->characters[i].key) +
                                "): " + source->error;
            for (const auto& note : source->result.errors)
                error += " " + note;
            throw std::runtime_error(error);
        }
        if (!map.plan_ready[i]) {
            auto plan = catalog_.Plan(source->id);
            const auto& saved = map.incoming->characters[i];
            if (!saved.task_catalog.empty())
                characters_.ConfigureTaskCatalog(plan, saved.task_catalog);
            if (saved.restore.task_selection)
                plan.visual_program->tasks->Resolve(saved.restore.task_selection->task);
            if (const auto& visual = saved.restore.visual_state)
                characters_.CheckVisualRestore(plan, *visual);
            if ((plan.package.appearance || plan.payload) && !private_models_)
                throw std::runtime_error("private model restoration requires the model release observer");
            map.plans[i] = std::move(plan);
            map.plan_ready[i] = true;
        }
        const auto source_view = snapshot->sources.at(source->id);
        if (map.action_views[i] == source_view)
            continue;
        const auto& saved = map.incoming->characters[i].action;
        const auto* configured = saved ? std::get_if<SavedConfiguredAction>(&*saved) : nullptr;
        const auto* visual = saved ? std::get_if<VisualActionKey>(&*saved) : nullptr;
        const auto& available = source->actions;
        std::optional<api::VisualReactionInput> death_names;
        const auto& saved_reaction = map.incoming->characters[i].restore.manual_reaction;
        if ((visual && visual->occurrence != DeathOccurrence::Configured) ||
            (saved_reaction && saved_reaction->occurrence != DeathOccurrence::Configured)) {
            death_names = catalog_.ResolveDeathNames(visual ? visual->character_config : saved_reaction->source);
            if (!death_names) {
                pending = true;
                continue;
            }
        }
        if (const auto& reaction = map.incoming->characters[i].restore.manual_reaction) {
            if (catalog_.InspectVisuals(map.plans[i].action_sources) ||
                !VisualActionSourceReady(available, reaction->source)) {
                pending = true;
                continue;
            }
            // Check this exact source and row, independently of whichever
            // ordinary body task was saved while the state remained active.
            (void)ResolveVisualAction(available,
                                      VisualActionKey{reaction->source, reaction->flag, false, reaction->occurrence},
                                      death_names ? &*death_names : nullptr);
        }
        if (visual && catalog_.InspectVisuals(map.plans[i].action_sources)) {
            pending = true;
            continue;
        }
        // Even a proposal seen early can gain exit tasks during discovery. A
        // configured/ready request waits for its required domain's complete
        // projection, not the other domain's unrelated ability graph.
        if (visual && !VisualActionSourceReady(available, visual->character_config)) {
            pending = true;
            continue;
        }
        if (saved && !visual && !std::holds_alternative<NpcMotion>(*saved) &&
            !std::holds_alternative<ProgramAction>(*saved) && !std::holds_alternative<BattleIdle>(*saved) &&
            !(configured && configured->controller_observed) &&
            !available.Ready(configured ? configured->domain : ActionDomain::Battle)) {
            pending = true;
            continue;
        }
        map.actions[i] = RestoreLayoutAction(saved, available, death_names ? &*death_names : nullptr);
        map.action_views[i] = source_view;
    }
    if (pending)
        return;
    map.old.clear();
    map.added.clear();
    for (auto id : characters_.Ids())
        if (characters_.Status(id).scene == map.incoming->scene &&
            characters_.Plan(id).persistence == CharacterPersistence::Layout)
            map.old.push_back(id);
    // All file/source/action preparation completed before touching old actors.
    for (auto id : map.old) {
        characters_.Delete(id);
        catalog_.ForgetCharacter(id);
        if (removed_)
            removed_(id);
    }
    map.phase = RestorePhase::Removing;
    map.status.phase = LayoutPhase::Removing;
}
void LayoutController::Install(Map& map) {
    if (std::any_of(map.old.begin(), map.old.end(), [&](auto id) { return characters_.Contains(id); }))
        return;
    std::map<std::uint64_t, std::uint64_t> keys;
    catalog_.BatchSources([&] {
        for (std::size_t i = 0; i < map.incoming->characters.size(); ++i) {
            const auto& saved = map.incoming->characters[i];
            CharacterRestoreState restore;
            restore.presentation_phase = saved.restore.presentation_phase;
            restore.playback_rate = saved.restore.playback_rate;
            const auto id = characters_.Add(map.incoming->scene, std::move(map.plans[i]), std::move(map.actions[i]),
                                            saved.placement, std::move(restore));
            keys.emplace(saved.key, id);
            map.added.push_back(id);
            characters_.SetEnabled(id, saved.enabled);
            catalog_.InspectActions(id, characters_.Plan(id).action_sources);
        }
        for (std::size_t i = 0; i < map.added.size(); ++i) {
            if (const auto& reaction = map.incoming->characters[i].restore.manual_reaction)
                characters_.RestoreVisualReaction(map.added[i], *reaction);
            if (const auto& saved = map.incoming->characters[i].restore.visual_state) {
                auto state = *saved;
                RemapVisualTargets(state, keys);
                characters_.RestoreVisualProgram(map.added[i], std::move(state));
            }
            if (const auto& saved = map.incoming->characters[i].restore.task_selection) {
                auto task = *saved;
                RemapTaskTargets(task, keys);
                characters_.RestoreTask(map.added[i], std::move(task));
            }
            auto look = map.incoming->characters[i].look;
            if (look.mode == LookMode::Instance)
                look.target = keys.at(look.target);
            characters_.Look(map.added[i], look);
        }
        Release(map);
        map.status.initialized = true;
        map.phase = RestorePhase::Applying;
        map.status.phase = LayoutPhase::Applying;
    });
    // The intended layout is now installed. Persist it even if the game later
    // refuses a particular actor; keep that actor's actual failure visible.
    map.captured.reset();
    map.incoming.reset();
}
void LayoutController::ObserveApplied(Map& map) {
    bool pending = false;
    for (auto id : map.added) {
        if (!characters_.Contains(id))
            continue; // a later user deletion wins
        const auto s = characters_.Status(id);
        if (s.deleting || !s.enabled)
            continue;
        if (!s.creation_error.empty())
            throw std::runtime_error("restored character " + std::to_string(id) + ": " + s.creation_error);
        if (s.action && s.action->phase == ActionIntentPhase::Failed)
            throw std::runtime_error("restored action " + std::to_string(id) + ": " + s.action->detail);
        if (s.tasks) {
            const auto& task = s.tasks->control;
            if (!task.preparation_error.empty() || !task.status.error.empty())
                throw std::runtime_error("restored task " + std::to_string(id) + ": " + task.preparation_error + " " +
                                         task.status.error);
            if (task.preparing || task.status.pending)
                pending = true;
        }
        if (!s.creation_ready || (s.action && (s.action->phase == ActionIntentPhase::Waiting ||
                                               s.action->phase == ActionIntentPhase::Dispatching)))
            pending = true;
    }
    if (!pending) {
        map.phase = RestorePhase::Ready;
        map.status.phase = LayoutPhase::Ready;
        map.status.busy = false;
    }
}
void LayoutController::Advance(const std::optional<SceneAddress>& scene) {
    if (closed_)
        return;
    const auto next = scene ? scene->key : std::string{};
    if (next != current_) {
        Capture(true);
        current_ = next;
    }
    if (scene && !maps_.count(scene->key)) {
        auto& map = maps_[scene->key];
        map.status.busy = true;
        map.status.phase = LayoutPhase::Reading;
        map.status.operation = LayoutActivity::RestoreCurrent;
        map.io = io_->Submit({LayoutIoKind::ReadCurrent, scene->key});
        io_->Submit({LayoutIoKind::List, scene->key});
    }
    for (auto& result : io_->Take()) {
        const auto found = maps_.find(result.request.scene);
        if (found == maps_.end())
            continue;
        auto& map = found->second;
        const auto kind = result.request.kind;
        if (kind == LayoutIoKind::WriteCurrent) {
            if (result.error.empty())
                map.status.saved_revision = std::max(map.status.saved_revision, result.request.revision);
            map.status.storage_error = result.error;
            continue;
        }
        if (kind == LayoutIoKind::List) {
            if (result.error.empty())
                map.status.names = std::move(result.names);
            if (result.id != map.io)
                continue;
        } else if (result.id != map.io)
            continue;
        map.io = 0;
        try {
            if (!result.error.empty())
                throw std::runtime_error(result.error);
            if (kind == LayoutIoKind::ReadCurrent || kind == LayoutIoKind::Load) {
                if (result.layout)
                    Begin(map, std::move(*result.layout));
                else {
                    map.status.initialized = true;
                    map.phase = RestorePhase::Ready;
                    map.status.phase = LayoutPhase::Ready;
                    map.status.busy = false;
                }
            } else {
                if (kind != LayoutIoKind::List)
                    map.status.names = std::move(result.names);
                if (kind == LayoutIoKind::Save && !map.status.initialized) {
                    // A successful explicit save accepts the current working
                    // arrangement after failed startup restoration. Resume
                    // automatic persistence of subsequent edits as well.
                    map.status.initialized = true;
                    map.phase = RestorePhase::Ready;
                    map.captured.reset();
                }
                map.status.busy = false;
                map.status.phase = LayoutPhase::Ready;
            }
        } catch (const std::exception& e) {
            Fail(map, e.what());
        }
    }
    for (auto& entry : maps_) {
        auto& map = entry.second;
        // Queries may be prepared on any safe exploration frame; replacement
        // and creation of this map can only commit when it is actually current.
        if (entry.first != current_)
            continue;
        try {
            if (map.phase == RestorePhase::Preparing)
                Prepare(map);
            if (map.phase == RestorePhase::Removing)
                Install(map);
            if (map.phase == RestorePhase::Applying)
                ObserveApplied(map);
        } catch (const std::exception& e) {
            Fail(map, e.what());
        }
    }
}
bool LayoutController::Replacing(const std::string& scene) const {
    const auto found = maps_.find(scene);
    if (found == maps_.end())
        return false;
    const auto& m = found->second;
    return m.phase == RestorePhase::Reading || m.phase == RestorePhase::Preparing || m.phase == RestorePhase::Removing ||
           (m.status.busy && m.status.operation == LayoutActivity::Load);
}
void LayoutController::CaptureMap(const std::string& scene, Map& map) {
    if (!map.status.initialized || Replacing(scene))
        return;
    try {
        auto value = CaptureSceneLayout(characters_, scene);
        if (map.captured && SameLayout(value, *map.captured))
            return;
        map.captured = std::make_shared<const SceneLayout>(std::move(value));
        ++map.status.revision;
        io_->Submit({LayoutIoKind::WriteCurrent, scene, {}, map.captured, false, map.status.revision});
        map.status.storage_error.clear();
    } catch (const std::exception& e) {
        map.status.storage_error = e.what();
    }
}
void LayoutController::Capture(bool force) {
    if (closed_)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - captured_at_ < std::chrono::milliseconds(250))
        return;
    captured_at_ = now;
    for (auto& entry : maps_)
        CaptureMap(entry.first, entry.second);
}
void LayoutController::Execute(const LayoutCommand& command, const std::string& scene, std::uint64_t serial) {
    if (closed_)
        throw std::runtime_error("layout service is closing");
    const auto found = maps_.find(scene);
    if (found == maps_.end())
        throw std::runtime_error("map working state not observed yet");
    auto& map = found->second;
    if (map.status.busy)
        throw std::runtime_error("this map already has a layout operation in progress");
    map.status.command = serial;
    map.status.name = command.name;
    map.status.error.clear();
    LayoutIoRequest request;
    request.scene = scene;
    request.name = command.name;
    request.overwrite = command.overwrite;
    switch (command.operation) {
    case LayoutOperation::Refresh:
        request.kind = LayoutIoKind::List;
        map.status.operation = LayoutActivity::Refresh;
        break;
    case LayoutOperation::Save:
        request.kind = LayoutIoKind::Save;
        map.status.operation = LayoutActivity::Save;
        request.layout = std::make_shared<const SceneLayout>(CaptureSceneLayout(characters_, scene));
        break;
    case LayoutOperation::Load:
        request.kind = LayoutIoKind::Load;
        map.status.operation = LayoutActivity::Load;
        break;
    case LayoutOperation::Delete:
        request.kind = LayoutIoKind::Delete;
        map.status.operation = LayoutActivity::DeleteSnapshot;
        break;
    case LayoutOperation::RetryCurrent:
        if (map.status.initialized) {
            // Explicit retry of a failed automatic write uses the CURRENT
            // values, never an older queued snapshot.
            map.captured.reset();
            CaptureMap(scene, map);
            map.status.operation = LayoutActivity::RetrySave;
            return;
        }
        request.kind = LayoutIoKind::ReadCurrent;
        map.phase = RestorePhase::Reading;
        map.status.operation = LayoutActivity::RestoreCurrent;
        break;
    case LayoutOperation::KeepCurrent:
        if (map.status.initialized)
            throw std::runtime_error("working state already initialized");
        map.status.initialized = true;
        map.phase = RestorePhase::Ready;
        map.status.phase = LayoutPhase::Ready;
        map.status.operation = LayoutActivity::KeepCurrent;
        map.captured.reset();
        CaptureMap(scene, map);
        return;
    }
    map.io = io_->Submit(std::move(request));
    map.status.busy = true;
    map.status.phase = LayoutPhase::Io;
}
LayoutSnapshot LayoutController::Snapshot() const {
    bool changed = !published_.available || published_.maps.size() != maps_.size();
    for (const auto& entry : maps_) {
        const auto found = published_.maps.find(entry.first);
        if (found == published_.maps.end() || !Equal(found->second, entry.second.status))
            changed = true;
    }
    if (changed) {
        published_.available = true;
        ++published_.sequence;
        published_.maps.clear();
        for (const auto& entry : maps_)
            published_.maps.emplace(entry.first, entry.second.status);
    }
    return published_;
}
void LayoutController::Close() {
    if (closed_)
        return;
    Capture(true);
    closed_ = true;
    io_->Seal();
    for (auto& entry : maps_) {
        Release(entry.second);
        if (entry.second.status.busy) {
            entry.second.status.busy = false;
            entry.second.status.phase = LayoutPhase::Cancelled;
        }
    }
}
}
