# Visual programs

`VisualProgramPlayer` runs an authored scene across body actions. `ActionPlayback::ConfigureVisualProgram` connects it to the animation player while the battle presentation domain is enabled.

## Host integration

Call `FrameworkHost::SetVisualPrograms` before preparation with a map from character configuration path to `VisualProgramInput`. The application loads definitions off the engine thread. An empty map leaves program observers uninstalled.

`VisualProgramInput` contains the definition, explicit targets and preview choices. `Caster` resolves to the created actor. Other target names use `targets` for live entity generations or `target_characters` for framework instance IDs. `SceneCharacters` resolves instance IDs at submission, after creation or restoration. Missing, retired and cross-world targets do not fall back to the caster. Custom assemblies supplying instance IDs must provide `resolve_character` and keep it alive through cleanup.

Optional `read_parameters(character)` supplies target IDs and choices when an action selection is consumed. Its values overlay the startup defaults; explicit `Select` choices take precedence. The player keeps that snapshot through the action. Existing effect requests retain their original targets. `SceneCharacters` binds the owning instance ID; custom assemblies provide it explicitly.

The host prepares `VisualProgramRuntime` and activates its construction, sequence and submission observers before servicing characters. A session declares its rules before loading its alternate model. On scene entry, the runtime reads and retains the current event receiver. Native effects and Common Timelines remain owned until their actual cleanup completes.

For custom assembly, use `CharacterPlan::visual_program` with `CharacterSessionServices::visual_program`, or configure `ActionPlayback` directly. Keep providers alive until sessions and program cleanup have closed.

Development builds optionally read `VISUAL_PROGRAMS.json` beside the DLL at startup. Each entry specifies a definition file (relative to the manifest), target instance IDs and choices. IDs refer to the current loaded layout, not permanent character identities. No file means no programs. The file does not create targets or start actions.

```json
{"programs":[{"file":"VisualPrograms/scene.json","targets":{"Target":2},"choices":{"variant":"a"}}]}
```

The development command channel accepts `visual_inputs` to replace one instance's parameter overrides. Omitted maps are empty; omitting both restores startup defaults. Definitions remain fixed until restart. The reply `visual_inputs_set` confirms storage, not playback. The command does not create instances, select an action or change a saved layout.

```json
{"op":"visual_inputs","character":7,"targets":{"Target":2},"choices":{"variant":"b"}}
```

## Data

`ParseVisualProgram` reads the `hksr.visual-program` format.

| Field | Content |
|---|---|
| `id`, `source`, `scope` | Program identity, character configuration and scene name |
| `effects` | Local keys mapped to resource paths, explicit targets and entity-effect options |
| `actions` | Body state, entry instructions, progress lanes and completion destination |

Each instruction has an `op` and a `source` identifying its configuration node. Operations are `create`, `assign`, `progress`, `remove`, `trigger`, `timeline_play` and `timeline_stop`. An optional `when` selects an input by `choice` and `is`.

`progress.value` is a number or `{"variable":"name"}`. Each lane contains ordered cues with `at` and `instructions`. Cue times use normalized body progress; effect transition durations use seconds. Lanes advance independently.

An action with `restart: true` creates a new scene. Other actions retain its effects and variables. Local keys do not set the engine's `UniqueEffectName`; `unique_name` is an explicit option.

`completion_state` requests a successor after `complete_at` and all lanes finish. `after_completion` runs when that successor is observed at `completion_at` (normalized progress, default 0). A successful play call alone does not run these instructions. If the controller enters the declared successor itself, the player observes it without replaying it. Without `completion_state`, the scene remains available for another action.

## Services and ownership

`VisualProgramServices::create` resolves targets, retains inputs and calls `VisualEffectContext::StartEntity`. Trigger and removal operations use the SDK effect services unless overridden. Removal forwards fade and immediate-fade flags and waits for a manager receipt, not the end of the fade. Timeline playback uses a supplied service with cancellation and drain observations.

`ConnectVisualTimelines` connects a `VisualTimelines` scene to these callbacks. Its factory creates SDK `OwnedTimeline` requests bound to the actor. The adapter supports Common Timelines without task-level volume release. Replaying a key retains the cancelled prior borrow until cleanup completes.

`Select` queues intent, merging supplied choices over the service defaults. `Service` prepares the scene and collects independent input errors. The body starts after preparation succeeds; `Played` and `Observe` identify its actual playback and model. Pending controls retry only unresolved receipts, without restarting accepted transitions.

Runtime diagnostics report selection, waiting instructions, native event attribution and effect targets. Timeline records include the registry borrow, observed phase and cleanup status; unchanged records are suppressed.

After successor instructions finish, existing effects and executing native producers retain their natural lifetimes. Stop looping Timelines explicitly in the instructions. `Leave` interrupts the scene, including retained tails, and permits a later restart. `End` closes the player. Continue `Service` until `Drained`, including effects, executing native producers and Timeline cleanup. `Service(false)` collects lifetimes without advancing instructions while a body replacement is pending.

For native events, declare `VisualEventRoutes` selectors before model event construction, then attach the current receiver and scene before playback. Definitions are reusable. Detaching prevents new associations; executing sequences keep their original cleanup responsibility until completion, reset or definition retirement.

Optional `effect_keys` map exact original `TriggerEffect.EffectPath` values to local keys. Unlisted effects use the rule's default key. Supply a source-path reader when using this mapping. `RemoveInFlight` also cancels later submissions from the currently bound execution; a later execution does not inherit that cancellation.
