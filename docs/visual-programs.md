# Visual programs

`VisualProgramPlayer` runs an authored scene across body actions. `ActionPlayback::ConfigureVisualProgram` connects it to the animation player while the battle presentation domain is enabled.

## Host integration

Call `FrameworkHost::SetVisualPrograms` before preparation with a map from character configuration path to `VisualProgramInput`. The application loads definitions off the engine thread. An empty map leaves program observers uninstalled.

`VisualProgramInput` contains the definition, explicit targets and preview choices. `Caster` resolves to the created actor. Other target names use `targets` for live entity generations or `target_characters` for framework instance IDs. `SceneCharacters` resolves instance IDs at submission, after creation or restoration. Missing, retired and cross-world targets do not fall back to the caster. Custom assemblies supplying instance IDs must provide `resolve_character` and keep it alive through cleanup.

Optional `read_parameters(character)` supplies target IDs, world anchors and choices when an action selection is consumed. Its values overlay the startup defaults; explicit `Select` choices take precedence. The player keeps that snapshot through the action. Existing effect requests retain their original inputs. `SceneCharacters` binds the owning instance ID; custom assemblies provide it explicitly.

The host prepares `VisualProgramRuntime` and activates its construction, sequence and submission observers before servicing characters. A session declares its rules before loading its alternate model. On scene entry, the runtime reads and retains the current event receiver. Native effects and Common Timelines remain owned until their actual cleanup completes.

For custom assembly, use `CharacterPlan::visual_program` with `CharacterSessionServices::visual_program`, or configure `ActionPlayback` directly. Keep providers alive until sessions and program cleanup have closed.

Development builds optionally read `VISUAL_PROGRAMS.json` beside the DLL at startup. Each entry specifies a definition file (relative to the manifest), target instance IDs and choices. IDs refer to the current loaded layout, not permanent character identities. No file means no programs. The file does not create targets or start actions.

```json
{"programs":[{"file":"VisualPrograms/scene.json","targets":{"Target":2},"choices":{"variant":"a"}}]}
```

The development command channel accepts `visual_inputs` to replace one instance's parameter overrides. Omitted maps are empty; omitting all restores startup defaults. Definitions remain fixed until restart. The reply `visual_inputs_set` confirms storage, not playback. The command does not create instances, select an action or change a saved layout.

```json
{"op":"visual_inputs","character":7,"targets":{"Target":2},"choices":{"variant":"b"}}
```

`visual_signal` queues a named event for a specific creation and program action. Read `generation` and `intent` from that instance's `visual-program` diagnostic. `visual_signal_queued` confirms mailbox storage; `visual-signal` reports engine-thread acceptance. Stale generations or action intents are not dispatched.

```json
{"op":"visual_signal","character":7,"generation":11,"intent":3,"name":"enter_view"}
```

## Data

`ParseVisualProgram` reads the `hksr.visual-program` format.

| Field | Content |
|---|---|
| `id`, `source`, `scope` | Program identity, character configuration and scene name |
| `effects` | Local keys mapped to resource paths, explicit entity targets or world anchors, and visual options |
| `actions` | Body state, entry instructions, progress lanes and completion destination |
| `signals` | Named instruction sequences submitted independently of body progress |
| `contexts` | Independent effect lifetimes with entry and exit instructions |

Each instruction has an `op` and a `source` identifying its configuration node. Operations are `create`, `assign`, `progress`, `remove`, `trigger`, `follow`, `effect_state`, `context_enter`, `context_exit`, `timeline_play` and `timeline_stop`. An optional `when` selects an input by `choice` and `is`.

Each effect specifies either `target` or `anchor`. An anchor resolves through `VisualProgramInput::anchors` or parameter overrides; it contains world `position` and an optional `[x,y,z,w]` quaternion `rotation` (identity by default). Anchored effects accept `scale`, `unique_name`, `fade_in` and `simulate_immediately`, but not entity-relative offsets or attachment options. No target entity is created or substituted. The initial pose is fixed at submission; the effect's own animation can move it.

```json
{"op":"visual_inputs","character":7,"anchors":{"center":{"position":[6,0,58],"rotation":[0,1,0,0]}}}
```

The corresponding effect definition is `{"path":"effect.prefab","anchor":"center"}`. Startup manifest entries also accept `anchors`. Entity-targeted effects may specify `attach_point` for initial attachment independently of `attach_to_target` for continued parenting.

`effect_state` sets the named effect's first Animator on layer zero. `transition` is normalized transition duration (default 0); `restart` defaults to false. The SDK resolves the state name through Unity's `Animator.StringToHash` and caches the result. This does not select the character's body state.

```json
{"op":"effect_state","effect":"ground","state":"Idle","transition":0,"restart":false,"source":"view.enter[0]"}
```

`follow` binds the named effect's entity-follow plugin to explicit target names. An empty `targets` array releases that binding. Targets must be loaded entities owned by this host in the same world. The instruction does not change the effect's Animator state.

```json
{"op":"follow","effect":"ground","targets":["Target"],"source":"scene.exit_view.follow"}
```

`remove.optional` lists keys whose absence is expected in some branches. Missing optional requests produce a note; matching requests are still removed. Other missing keys and failed removal calls remain errors. Optional keys must also appear in that instruction's `effects` list.

`progress.value` is a number or `{"variable":"name"}`. Each lane contains ordered cues with `at` and `instructions`. Cue times use normalized body progress; effect transition durations use seconds. Lanes advance independently.

An action with `restart: true` replaces the default action context. Other actions retain its effects and variables. Named contexts are independent of this replacement. Local keys do not set the engine's `UniqueEffectName`; `unique_name` is an explicit option.

`completion_state` requests a successor after `complete_at` and all lanes finish. `after_completion` runs when that successor is observed at `completion_at` (normalized progress, default 0). A successful play call alone does not run these instructions. If the controller enters the declared successor itself, the player observes it without replaying it. Without `completion_state`, the scene remains available for another action.

## Services and ownership

### Independent contexts

A named context has `enter` and `exit` instruction arrays. `context_enter` creates it once; repeated entry keeps the same effects and token. `context_exit` runs its exit instructions, detaches future native producers and cancels remaining receipts. Re-entry creates a new token. Action completion, restart and unlisted body states do not exit named contexts.

```json
{
  "contexts": {
    "aura": {
      "enter": [{"op":"create","effects":["ground"],"source":"aura.create"}],
      "exit": []
    }
  }
}
```

Use `{"op":"context_enter","name":"aura","source":"phase.enter"}` or `context_exit` in action instructions. An effect instruction in an action may select `"context":"aura"`. Context entry, exit and signals use their own effect keys, variables, choices and captured inputs. These sequences support effect operations and assignment; nested context changes and Timelines are not supported. Timelines remain action-owned.

An optional `event_scope` selects that context's `PresentationProfile.native_events` rules. Declare these rules before model construction. Context exit stops new associations; already-running native sequences retain the old context for late submissions and cleanup.

Scoped signals use an object instead of the action-scoped instruction array:

```json
{"signals":{"enter_view":{"context":"aura","instructions":[
  {"op":"effect_state","effect":"ground","state":"Idle","source":"view.enter"}
]}}}
```

For this signal, pass `ContextToken("aura")` to `Signal`, or its diagnostic `context=[aura:token]` value as the development command's `intent`. It remains valid across body selections until that context exits. Signals run in order within each context; a pending context does not block another. Named contexts retain their entry inputs and the first resolved target generation; they do not adopt a rebuilt target automatically.

`InterruptAction` cancels only the default action context and its queued signals. `Leave` and `End` cancel all contexts directly, without waiting for authored exit instructions that may require a live model or loaded effect. Keep servicing until every receipt and native producer drains. The host uses whole-scene exit for domain changes, model retirement and deletion.

### Effect services

`VisualProgramServices::create` resolves inputs and calls `VisualEffectContext::StartEntity` or `StartSpatial`. Trigger and removal operations use the SDK effect services unless overridden. Removal forwards fade and immediate-fade flags and waits for a manager receipt, not the end of the fade. Timeline playback uses a supplied service with cancellation and drain observations.

`ConnectVisualTimelines` connects a `VisualTimelines` scene to these callbacks. Its factory creates SDK `OwnedTimeline` requests bound to the actor. The adapter supports Common Timelines without task-level volume release. Replaying a key retains the cancelled prior borrow until cleanup completes.

`VisualEntityMotion` is shared by all host scenes. Each target can belong to one motion binding at a time. Repeating the same binding does not reset its accumulated movement; changing targets requires releasing the previous binding. Existing official attached motion cannot be adopted by inspecting its flag.

`VisualEffectContext` releases bindings before servicing explicit effect cancellation and records natural request completion without reading pooled request objects. Caster or target disposal also releases the binding before components are destroyed. Failed detachment is logged; it does not prevent official entity disposal or request cancellation. Custom assemblies provide `follow`, call `UseEntityMotion`, and retain the motion service and effect requests through cleanup.

`Select` queues intent, merging supplied choices over the service defaults. `Service` prepares the scene and collects independent input errors. The body starts after preparation succeeds; `Played` and `Observe` identify its actual playback and model. Pending controls retry only unresolved receipts, without restarting accepted transitions.

For action-scoped signals, `Signal(intent, name)` requires the prepared action's token. Unknown names throw; stale tokens return zero. Signals retain their cursors across pending loads and do not wait for a body clock. New action selection or completion discards queued action signals, but not context-scoped signals. Custom hosts can supply `take_signals(character)` to marshal value-only messages to the runtime's owner frame.

Runtime diagnostics report selection, waiting instructions, native event attribution and effect targets. Timeline records include the registry borrow, observed phase and cleanup status; unchanged records are suppressed.

After successor instructions finish, existing effects and executing native producers retain their natural lifetimes. Stop looping Timelines explicitly in the instructions. `Leave` interrupts the scene, including retained tails, and permits a later restart. `End` closes the player. Continue `Service` until `Drained`, including effects, executing native producers and Timeline cleanup. `Service(false)` collects lifetimes without advancing instructions while a body replacement is pending.

For native events, declare `VisualEventRoutes` selectors before model event construction, then attach the current receiver and scene before playback. Definitions are reusable. Detaching prevents new associations; executing sequences keep their original cleanup responsibility until completion, reset or definition retirement.

Optional `effect_keys` map exact original `TriggerEffect.EffectPath` values to local keys. Unlisted effects use the rule's default key. Supply a source-path reader when using this mapping. `RemoveInFlight` also cancels later submissions from the currently bound execution; a later execution does not inherit that cancellation.
