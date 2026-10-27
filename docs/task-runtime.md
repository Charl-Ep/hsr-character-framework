# Task runtime

The `tasks` module provides typed visual tasks, configuration loading, owned-resource execution and per-actor selection. The menu and HTTP use the same application task directory. An instance can bind one task catalog and retain its selection in map layouts. A complete character catalog and multiple task-catalog domains per instance are not implemented.

`CharacterTasks` lists NPC motions, configured animation candidates, ready transitions, shared reactions, authored programs, task sequences and idle. Each row retains its backend identity, sources, availability and application command. A configured animation candidate is not a complete battle ability. The menu shows available rows and tasks whose missing inputs can be edited. Development mode can show other unavailable and unknown rows. HTTP retains all rows and their reasons.

Shared reactions use the `VisualState` executor for configured enter/hold/recover body phases, expression and blink ownership. Task sequences can reference this executor through `reaction`; material, speed and floating behavior are not implemented. See [action API](http-api.md#actions).

`hit` dispatches a source `HitAnimation` to an explicit owned target. `CharacterSession` binds the receiving actor's configuration cache, control sources and animation session. `ReactionPolicy` selects its reaction type, name and weight. Motion height is read from the receiver's movement component. Launch requests are not implemented.

Optional `direction: {"horizontal": 0, "vertical": 0}` captures the sender's logical position and applies the receiving model's hit angle before playback. Angles are degrees; a random contribution must already be resolved. Geometry uses the receiver's base character configuration and current attachment. Runtime shape overrides are not supplied by this command.

Optional `effect` carries `path`, `height`, `angle`, `delay`, `minimum_interval`, `maximum_count` and `offset_by_scale`. All fields are required; an empty path allows receiver overrides when the source animation is nonempty. The receiver admits effects independently of animation throttling, checks the official mutex rules and owns submitted requests through completion or model/domain retirement. Source cancellation cannot remove a delivered effect. Reuse and replay are limited to the receiver's owned hit receipts.

`add_source.hit_effects` supplies scoped overrides. Each row specifies `stacking` (`highest` or `multiple`), `path` (string or null), and `priority`; optional fields are `death_path`, `count`, `death_count`, `offset` (XYZ), `keep_original` and `replay_on_mutex`. Updating a source advances its effect version. Removing it removes its override contribution, not effects already delivered to another lifetime. Random/key/inherited AttackData sources must be resolved before dispatch.

`ActionPlayback::QueueHit` binds resolved layer and expression commands to the current model. Additive requests retain the body action; body requests replace it. `ReleaseHitExpression` and `ReleaseHit` retire separate lifetimes. Clip completion does not return the body to idle. A delivered hit belongs to the receiver: ending the attack only retracts an impact that has not been admitted.

Receiver configuration is loaded on demand through `CatalogBrowser` and shared with action-menu inputs. It does not enumerate skills or abilities. `add_source.behaviors` contains the contributing `ModifierBehaviorFlag` values, separately from its visual `flag`. An empty array contributes none; omission leaves those inputs unknown. Only values used by the selected reaction branch are required. Hit expression duration is independent of body playback.

Production hits resolve the current visual/default expression when their expression timer ends. Body-return observation requires the configured request boundary, state departure, an alive grounded receiver and the current return animation. It releases playback ownership without replaying that animation. Other successors remain untouched. This observes ordinary controller returns; it does not schedule a missing successor or implement native Extra holds. Raw `QueueHit` calls retain explicit release semantics unless `receiver_return` is enabled.

## Model

| Type | Responsibility |
|---|---|
| `TaskSourceInventory` | Configured skills, entry/prepare references and skill-owned ability groups |
| `TaskDefinition` | Visual task identity, typed inputs, prerequisites and compiled-sequence key |
| `TaskContext` | Per-actor phases, values, slot overrides and disable contributions |
| `TaskSequenceExecution` | One execution's cursors, waits, request receipts and exit reason |
| `TaskPresentation` | A sequence bound to owned animation players and a visual resource scope |
| `TaskCatalog` | Authored tasks, sequences and shared visual resources for one model domain |
| `TaskController` | Selection, configured successors, interruption and persistent context contributions |
| `BattleControl` | Logical selection, performance, insertion queue and source-owned reaction candidates |
| `SkillSlots` | Default/current skills, parent-child selection, source-key replacements and owned slot occupation |

`BuildTaskSources` consumes `OfficialCatalog::ReadTaskConfig`. It preserves skills without ready poses and reports missing or ambiguous literal group owners. Inventory entries are not execution permissions.

`TaskContext::Query` returns available, unavailable or unknown, with all reasons. Explicit inputs override context values and defaults for that query only. Context values retain their owner. Removing one context restores earlier contributions; repeated removal does nothing. Querying does not execute official predicates or change the game.

Target, spatial-anchor and visual-choice requirements use the visual scene's resolved input view. Target presence is checked against owned entity generations and world identity without engine reads. The caller cannot supply that availability. Inputs are rechecked when a queued task starts, then captured for its branches and resource scope. Resource loading and visual completion remain separate observations.

### Position targets

`visual.target_defaults` declares an owner-local position or a target alias:

```json
{"target_defaults": {
  "AbilityTargetEntity": {"local_position": [0, 0, 3]},
  "SkillPointEntity": {"alias": "AbilityTargetEntity"}
}}
```

The position is captured from the actor's world pose when the execution scope starts. Entity selections take precedence; a selected entity that becomes unavailable does not fall back. A cleared selection uses the configured default. Target aliases share the resolved input without creating another actor.

Target requirements accept positions only with `allow_position: true`. Facing and target movement consume the captured point. Effects and `hit` also require `allow_position: true`; entity attachment and follow operations still require an entity. Spatial effects apply local position and Z-X-Y Euler offsets to the captured pose. An initial attachment-point lookup is replaced by that pose only for the spatial branch.

A position `hit` emits the base effect without a receiver reaction, hitbox or modifier override. Delay, minimum interval and maximum live count are scoped to the sender's execution and target name; official entity-wide mutex rules are not used. The sender owns the effect through natural completion, cancellation and model/world retirement. Empty base paths are no-ops. Captured world poses are stored in `position_targets` for context restoration.

## Execution

Sequences contain submissions, request waits, second/frame waits, serial/parallel groups, branches and finite or finish-requested loops. Leaf operations are animation, movement, sound or existing `VisualInstruction` values.

Create an execution with a unique token, immutable sequence and `TaskSequenceServices`. Call `Advance(delta, frame)` once per engine frame. Sequential waits carry unused time; parallel branches advance independently. Cooperative yields retain cursors and time credits. An asynchronous completion without a timestamp does not supply inferred remaining frame time.

The kernel allocates a request serial and passes it with the execution token to `submit`. The adapter resolves owned participants. An unsubmitted request may await entity creation or model binding with generation zero; the first resolved generations cannot change later. Include synchronous callbacks in the initial state; later callbacks use `Observe`. Submission, request completion, performance completion and drain are separate observations. Progress is monotonic within one receipt. An error observation stops the execution and records the request identity.

`RequestFinish` lets the current iteration finish before its successor. `Stop(Interrupted/Replaced)` runs the configured interruption branch. Actor loss, world exit and deletion skip that branch. Normal finish may retain natural tails; later deletion can cancel them. Each receipt receives at most one normal-finish and one cancellation notification.

Keep the execution and service providers alive until `Drained()`. Destruction performs no engine cleanup. Failed or partially submitted resources remain the adapter's responsibility until their drain observation arrives.

Reusing a request name starts a new serial after the prior performance finishes. The old request receives normal finish; once drained and no longer referenced by a cursor, it is discarded. Continuous loops do not retain a history of every completed request.

### Ability calls

`call_ability` launches an owned visual sequence at the call site. `wait_for_performance` is required; `wait_abilities` joins only registered abilities. An executing identity is not invoked again. A completed identity can run again, retaining its waiting registration within the execution.

```json
{"op":"serial","children":[
  {"op":"call_ability","ability":"phase2","wait_for_performance":true,
   "child":{"op":"wait_seconds","seconds":0.3}},
  {"op":"wait_abilities"}
]}
```

Each invocation has its own request names. A child may reference a request in its calling scope, including a later submission. Diagnostic receipt names include the ability and invocation number. Execution status exposes each ability's source, invocation, waiting registration and execution state.

Non-waiting abilities keep running after control is yielded. `ProducersComplete()` becomes true after the root and all invoked bodies finish; resource drain can occur later. Cancellation stops every invoked body. These are maintained visual sequences, not native battle ability instances.

## Presentation adapter

Create `TaskPresentation` after the owned actor's model/domain handoff is ready. Supply its `VisualProgramPlayer`, resolved parameters and an animation resolver. `BindTaskAnimation` binds an existing `ActionPlayback`; `partner:<key>` resolves a declared partner through that visual scene. Unknown participants fail; a declared participant under construction can wait.

Service animation players on their normal owner frame, then call `TaskPresentation::Service(delta, frame)`. Keep the task, scene and bound players alive until the task's `Drained()` returns true. Actor deletion and world departure also close the visual scene through its owner lifecycle.

| Operation | Backend | Completion observation |
|---|---|---|
| Actor body animation | Existing FreeStyle playback | Current/next state and normalized time |
| Other animation layer | Animator play or cross-fade | That layer's state and time |
| Partner body animation | Existing partner idle-animation service | Partner's own Animator state and time |
| Body sound event | SDK AudioManager request | Separate start and end callbacks |
| Body movement | SDK owned animation-movement request | Official evaluator progress and completion |
| Visual instruction | `VisualProgramPlayer` instruction executor | Instruction acceptance/application |

Animation requests do not select an automatic successor. A completed non-loop state or observed exit completes its wait; looping states remain active. Releasing a receipt stops observation, not the Animator. The next task command owns the next state. Controlled domain changes cancel old requests, while observed Animator changes invalidate their bindings.

An animation submission accepts `start` (normalized playback offset, default `0`) and `transition` (normalized blend duration, default `0`). Body, partner and layer playback preserve the offset. Progress waits use the observed absolute animation time, not elapsed time since submission. A start offset does not itself count as observed entry or progress.

Optional `end` supplies a separate `animation_end` wait boundary. A positive value completes at that observed normalized time or observed state departure; a nonpositive value completes after observed entry. `state_left` waits only for departure. Neither means resource drain. Observation continues after clip completion until release or departure.

Optional `track` queues requests on the participant's animation layer. It requires `end` and explicit `logic`, `extra`, `release_on` and `force` fields. An incoming Extra matching an earlier request's `release_on` releases its hold and inserts after that record; otherwise it appends. `force` advances to the new record immediately. `animation_finished` observes the local request notification, separately from clip completion and state departure. Advancement preserves observations and reports supersession for unmet waits. Native event broadcasts, delay records and task-listener callbacks are not used.

`skill_recovery` requires a request name and the owned body endpoint. It resolves the current Phase/Visual return and expression, then queues Idle/SkillFinishRecover. With one pending successor, return waits for the current animation's End and normalized time one, or observed departure. `accepted` means recovery was admitted; `performance` means its body command was submitted. A naturally finished sequence retains the queued body until that submission. Cancellation removes the owned return request. Reaching an existing return state does not replay it. The operation does not reset gameplay state or grant control permission.

`ready_pose` selects `{config, skill, variant}` by configuration path and original skill/variant indices (`variant` defaults to `-1`). `reaction` selects `{config, flag, recover}` (`recover` defaults to `false`). Both require a request name, operate on this task controller's body and use cached sources bound to that actor. `BindTaskPose` reuses `ActionPlayback` transitions, expression and blink ownership; it does not grant control permission.

A ready performance wait completes when the destination or its controller successor is observed. A reaction occurrence remains active through its hold; recovery completes without scheduling an implicit idle. These composite operations have no normalized progress clock. Natural completion/replacement releases observation while retaining the selected pose for the next transition. Cancellation retires the owned pose; a stale receipt cannot cancel a newer one.

Use a performance wait for animation completion. A progress wait requires that exact threshold to have been observed; if the request finishes below it, execution reports an unsatisfied dependency instead of waiting forever or assuming the missed cue ran.

A later direct animation on the same participant and layer supersedes this execution's earlier observation. Its last progress is retained and `superseded` is reported separately from natural completion. Already-satisfied waits remain valid; unmet waits fail. External interruption still reports an execution error. Different participants and layers remain independent.

Submissions are asynchronous. Add explicit request waits for dependencies, including before reusing a request name. A visual creation instruction completing means the creation command was accepted, not that its effect finished. Effect controls retain and retry their exact pending receipts.

Sound uses `{"op":"sound","request":"voice","participant":"body","event":"event_name"}`. Its `accepted` wait observes queue acceptance, `started` observes the playback-start callback, and `performance` observes a successful end callback. A completed sequence retains a natural sound tail; cancellation selects the owned request by its callback and emitter, not its logical ID alone. The current host supports the body emitter, not partner or arbitrary world emitters. A waiting request dropped by the engine reports an error; missing callbacks do not imply successful completion or trigger automatic replay.

Movement references a preceding base-layer body animation request in the same execution. It waits for that playback to enter, then resolves its endpoints once and submits to the official movement component. Animation `started` waits also report actual entry. A later animation first cancels and drains its previous movement; a natural sequence finish retains movement until completion. Effect-follow motion and animation movement cannot occupy the same entity channel simultaneously.

The SDK acquires an exclusive native movement host on demand. It registers the required component on Story actors, pauses ordinary root translation, and waits for previously submitted movement to drain before starting the graph. Effect-follow uses the same lease. Cancellation and model handoff detach request-owned motion, restore the previous policy and remove only a component added by the lease.

```json
{"op":"movement","request":"approach","animation":"body",
 "segments":[
   {"destination":"target","name":"Target","stop_distance":1.5,"start":0,"end":0.3},
   {"destination":"origin","start":0.8,"end":1}
 ]}
```

`clock` is `animation` (normalized times) or `speed` (distance/speed intervals). Each segment accepts `start`, `end`, `speed`, `exit_time`, `offset` as world-space `[x,y,z]`, `preserve_height` (default true) and `stop_distance` (default zero). `destination` is `target`, `anchor` or unnamed `origin`. Targets and anchors use the captured sequence inputs. Origin is the body's position at the first movement in that sequence; an explicit anchor can provide another return point. `force_start` defaults to false. Targets are position snapshots; automatic radius lookup, expression evaluation, moving-target tracking, curve-resource loading and partner movement are not provided.

Movement `accepted` means adoption, `started` means evaluation began, `progress` uses the movement clock and `performance` requires an observed natural completion. A graph disappearing without that receipt reports interruption. Cancellation leaves the current position and clears only the request's own graph.

Each task gets a resource scope using the existing effects, partner and Timeline owners. Natural completion can retain tails; cancellation ends that scope. Named contexts outlive individual tasks until explicitly exited or the scene closes. `TaskPresentation::Drained()` includes scope cleanup even after the sequence kernel has finished. Old scope cleanup cannot detach a newer scope's event producer.

Visual instructions use their execution's resource scope unless `context` names a persistent context. Explicit `"context": null` selects the execution scope. Effects intended to finish naturally after a context exits must belong to a separate scope.

## Selection

`TaskController` owns one catalog on an already-prepared actor/domain. `Select` queries the requested task and optional slot override without native calls. An unavailable selection leaves the running task unchanged. The latest queued selection wins; an interruption already in progress is allowed to finish before it starts.

The controller waits for old producer work to be accepted or cancelled, not for independent visual tails to disappear. Final commands in a completed interruption branch are delivered before control passes. Natural completion uses only the configured `next` task; it does not infer StandBy. Successors are re-queried against the current context and at most one starts per frame.

Visual contexts contribute values, phase, slot replacements and disables only after entry completes. Exit revokes those contributions. Precedence follows actual entry order. Removing a context restores earlier owners; no official combat values are changed.

`Reset` cancels execution and leaves the visual scene for a domain/action change. `Close` also permanently closes the scene. Keep servicing until drained. A later task can reuse a reset controller.

The character service coordinates model preparation before starting a task. It restores any previous gaze binding through the ordinary domain-handoff path, then prepares the catalog's model domain. No preceding idle/action command is required. Re-selection replaces pending intent; cancel removes it; finish can be queued during gaze preparation. World departure or a battle frame cancels unstarted task intent. Preparation errors are published separately from execution errors.

## Control relations

A task may declare `control` independently of its sequence. The relation contains `label`, `trigger` (`user`, `event`, or `automatic`), `sources` and `requires`. Conditions are conjunctive. `event` identifies an explicit local visual-event input; it does not broadcast a game event. Automatic entries are successors, not selectable menu items.

Top-level `automatic` lists automatic task keys in authored priority order. On a prepared model-domain frame, the first eligible entry is accepted if no request is pending, with at most one admission per frame. Its conditions and changes must describe when it becomes eligible and stops being eligible. A controlled `next` destination must also use `trigger: automatic`.

| Condition `op` | Fields |
|---|---|
| `selected` | `skill`: `{source, index, slot?}` or `null` |
| `slot_skill` | `slot`, `skill`: `{source, index, slot}`, optional `child` index |
| `slot_available` | `slot` |
| `performance` | `in`: array of `ready`, `accepted`, `working`, `finished`, `resetting`, `failed` |
| `source` | `id`, optional `present` (default true) |
| `queued` | `id`, optional `present` (default true) |
| `insertion` | `phase`: `preparing`, `waiting`, `executing`, `finishing`, or `null` |
| `unresolved` | `source`, `detail` |

Relations declare changes in `accepted`, `started`, `completed`, `interrupted`, `replaced` and `failed` arrays. Supported changes are `select` (`skill`), `performance` (`phase`), `add_source` (`id`, `flag`, `priority`), `remove_source` (`id`), `queue` (`id`, `skill`), `enter_insertion` (`id`), `insertion` (`phase`), `leave_insertion`, and `withdraw` (`id`). Each exit uses only its declared changes.

Accepted changes commit before presentation starts. A presentation receipt drives `started`; sequence completion drives `completed`. Each logical channel retains its revision, so an old completion cannot overwrite a newer selection or renewed source. World/domain reset invalidates outstanding receipts. Visual resources remain owned by the existing presentation scopes.

`presentation: false` defines a value-only operation without `entry` or `next`. It does not interrupt playback, switch model domains or replace the saved action. Queue insertion, entry, withdrawal and return are distinct operations. A relation selects the pending request to enter; this module does not infer scheduling priority. Entry saves selection/performance for logical return, not a suspended animation clock. Body-reaction candidates use lower numeric priority first, preserving source order on ties. Filtering eligible candidates and selecting their visual plans belong to the authored relations; speed and forbid arbitration are not implemented.

For controlled catalogs, queries include separate `facets.path`, `facets.inputs` and `facets.binding`. A known path can have an unimplemented visual binding. Menu and HTTP dispatch recheck the same query. Normal controls and diagnostic previews are separate directory entries; the development menu exposes the latter as **Action previews**.

Control relations currently require authored configuration. Full character bindings, automatic event delivery and persistence of the logical queue/source state are not supplied by this module. Existing layout persistence retains task selection and visual context recipes only.

### Skill slots

Optional `control_slots` defines exact skill keys and initial slots. Skill identities contain the configuration source and original index; `children` contains keys in parent-list order. A child index does not index the complete skill list.

```json
{"skills":[
  {"skill":{"source":"character/config","index":7},"key":"Skill01","children":[]}
],"defaults":[{"slot":0,"key":"Skill01"},{"slot":1,"key":null}]}
```

Control changes support:

| `op` | Fields | Result |
|---|---|---|
| `map_slot` | `slot`, `key` (string or null) | Change the requested slot key; defer while occupied |
| `replace_skill` | `owner`, `from`, `to` (string or null) | Add/update one owned key replacement; null removes it |
| `occupy_slot` | `slot`, `owner` | Save the requested key and acquire occupation |
| `release_slot` | `slot`, `owner` | Release that owner and apply the deferred or saved key |
| `disable_slot` | `slot`, `owner`, `disabled` (default true) | Add/remove one owned disable contribution |

Key lookup uses the newest matching replacement once, not recursive alias expansion. Updating an existing owner does not change its precedence. Key replacement updates matching slots even while occupied. Direct slot remapping is deferred instead. A missing resolved key falls back to that slot's default. Assigning a skill clears the previous bound slot, including a binding inherited from its parent; it does not restore the other slot's default.

`slot_available` checks that a defined slot has a current skill and no owned disables. Other eligibility conditions remain in the relation. Snapshot `control.slots` exposes requested, default and current keys, mappings, occupation and disable owners. Reset restores the configured initial slots.

## Configuration

`ParseTaskCatalog` reads `hksr.task-catalog`. `visual` uses the resource, context and signal fields of a visual program, without its `format` or `actions`. `sequences` hold compiled plans; `tasks` reference them. A task without `entry` remains queryable as unimplemented.

```json
{
  "format": "hksr.task-catalog",
  "id": "example",
  "domain": "battle",
  "visual": {
    "id": "example-scene",
    "source": "Config/Character/Example.json",
    "scope": "example",
    "effects": {}
  },
  "sequences": {
    "gesture": {
      "entry": {
        "op": "serial",
        "children": [
          {"op": "animation", "request": "body", "participant": "body", "state": "Gesture"},
          {"op": "wait_request", "request": "body"}
        ]
      }
    }
  },
  "tasks": {
    "gesture": {"entry": "gesture"}
  }
}
```

State names and resource paths must match the selected model. A sequence may also define `interruption`. Node `source` is optional provenance; omitted sources use their configuration path.

| Node `op` | Fields |
|---|---|
| `serial`, `parallel` | `children` |
| `animation` | `request`, `participant`, `state`; optional `layer`, `transition` |
| `movement` | `request`, `animation`, `segments`; optional `clock`, `force_start` |
| `sound` | `request`, `participant`, `event` |
| `control` | `request`, `changes` using control-relation changes |
| `visual` | `request`, `instruction` using the existing visual instruction schema |
| `wait_request` | `request`; optional `signal`: `submitted`, `accepted`, `started`, `performance` (default), `progress`, `drained`; `progress` requires its numeric value |
| `wait_seconds` | `seconds` |
| `wait_frame` | No additional fields |
| `repeat` | `child`; `count` and/or `until_finish` |
| `branch` | `condition`, `yes`, `no` |

Task fields: `sources` (`document`/`field`), `inputs`, `requires`, `tags`, `entry`, and `next` (`task` plus optional `inputs`). Input definitions use `type` (`bool`, `integer`, `number`, `text`) and optional `default`. JSON integer and floating-point types are distinct. Requirements use `op`: `value` (`name`, `is`), `phase` (`is`), `participant` (`name`), `target` (`name`), `anchor` (`name`), `choice` (`name`, `is`), or `unresolved` (`source`, `detail`).

Optional `bindings` describes editable visual inputs:

```json
{
  "targets": ["Target"],
  "anchors": ["Destination"],
  "choices": {"mode": ["ordinary", "enhanced"]}
}
```

Target and anchor requirements also expose their named input. A choice requirement supplies its single value unless `bindings.choices` declares options. Required inputs and branch-only inputs remain distinct: declaring a binding does not make it required. Supplied choices must belong to their declared options. `Caster` is not editable.

Clicking a task with inputs opens the parameter editor. It queries through the same application command as HTTP, without starting playback. Edits and changes to selected targets or context ownership invalidate the previous result; late replies cannot enable an edited selection. Play submits the typed selection and rechecks current conditions on the official frame. Default removes an explicit override. The editor does not write context values or gameplay state.

Top-level `contexts` binds visual-context names to `values`, `phase`, `slots` (`slot`/`task`) and `disables` (one of `task`, `slot`, `tag`, plus `reason`). Task references are local to the catalog. Unsupported fields and broken references are reported during loading.

A `control` step commits value-only changes on the accepted operation's control ticket. It can follow animation progress, a request completion, a branch or an interruption step. All writes in one step commit together. Changes to channels replaced by a newer operation are not applied. The containing task must declare a control relation; the step does not invoke native battle tasks or broadcast game events.

## Development control

Both builds read `VISUAL_PROGRAMS.json` beside the Framework DLL on startup. Its `programs` array accepts `file`, `targets`, `choices` and `anchors`. Visual programs register source defaults; task catalogs register by catalog ID and do not change those defaults. Files are parsed on the worker before runtime initialization.

HTTP `create` and development `create_prepared` accept `task_catalog`. The catalog source and model domain must match the prepared character. Set `transient: true` for a session-only preview. An omitted `transient` creates a layout-persistent instance. One configured visual scene owns its resources; replacing a live resource catalog is not supported.

`reload_tasks` rereads this manifest and its files on the worker, then publishes the task catalogs through the application queue. Its reply contains `task_catalog_revision`. Invalid or missing input leaves the registry unchanged; an explicit empty list removes the published candidates. Source-default visual programs are not replaced by this command.

Existing previews retain their original definitions and resources. Subsequent `create_prepared` requests use the new publication; snapshots identify their `task_catalog` and `task_catalog_revision`. Delete an old preview and create another to exercise changed definitions without restarting the game. Publication does not create, rebuild or play characters.

Create a preview with `create_prepared`, a prepared `source_request`, `task_catalog` and `transient: true`. Selecting a task prepares its model domain through the existing handoff.

Session-only instances appear in snapshots with `transient: true`, but are omitted from automatic saves and named layouts. Loading a layout leaves them untouched. Source replacement preserves their lifetime. Saved actors cannot retain references to session-only targets in gaze, tasks or visual contexts. Delete previews through the ordinary character lifecycle.

The existing development command envelope accepts:

- `task_query`: return current execution, context values and task availability; optional `catalog`/`task` queries one task.
- `task_select`: requires `catalog` and `task`; accepts `inputs`, `slot`, `targets`, `choices`, `anchors`, and `trigger` (`user` or `event`).
- `task_finish`: finish the selected continuous task at its configured boundary.
- `task_cancel`: run its interruption and cancel retiring tails.

All require `character` and `creation` from the current snapshot. `body` binds the owned actor, and `partner:<key>` binds a declared visual partner. Commands run through `ApplicationService` on the exploration-world frame; they cannot target official battle actors. HTTP uses the same codec and commands. Snapshots and SSE include task state, context owners, typed values and availability; detailed request receipts are copied only on an explicit query.

Query results include active and retiring request receipts: authored name/source, execution, participant, entity/model generations, submission, acceptance, performance completion, progress and drain. These are copied from existing observations only when queried; reading them does not query the engine or retain execution history.

Transport acceptance is separate from task availability, execution and drain.

## Persistence

Layouts retain the catalog ID, selected task, typed inputs, choices, spatial anchors and logical target references. A started task retains its resolved identity and captured inputs; a configured successor replaces the saved selection. Cancellation clears it. Request receipts, native handles and playback clocks are not saved.

Captured inputs do not reread runtime defaults on restoration. A deleted target is saved as an unbound alias (`null`), not a reusable instance ID. Tasks requiring it report the missing target; they do not substitute the caster. Targets still being constructed are awaited, while disabled or failed targets are reported explicitly.

Restoration resolves the catalog before replacing existing actors, remaps targets to the new instance IDs, prepares the catalog's model domain and rebuilds visual contexts before querying the task. Playback restarts at its entry. Context values and slot contributions come from those restored contexts. Persistent resources require explicit `restore` recipes in the visual configuration; missing recipes are reported as save/restore errors.

A missing catalog or task fails layout preparation without removing existing actors. A runtime preparation or execution failure remains visible on the restored instance. Temporary previews remain excluded from persistence.
