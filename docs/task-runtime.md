# Task runtime

The `tasks` module provides typed visual tasks, configuration loading, owned-resource execution and per-actor selection. The menu and HTTP use the same application task directory. An instance can bind one task catalog and retain its selection in map layouts. A complete character catalog and multiple task-catalog domains per instance are not implemented.

`CharacterTasks` lists NPC motions, configured animation candidates, ready transitions, shared reactions, authored programs, task sequences and idle. Each row retains its backend identity, sources, availability and application command. A configured animation candidate is not a complete battle ability. The menu shows available rows by default; development mode can show unavailable and unknown rows. HTTP retains all rows and their reasons.

Shared reaction rows currently use the existing `VisualState` executor, exposed through the action catalog, menu, HTTP and layout persistence. It handles configured enter/hold/recover body phases, expression and blink ownership. These rows are not compiled `TaskSequence` plans; material, speed and floating behavior are not implemented. See [action API](http-api.md#actions).

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

`BuildTaskSources` consumes `OfficialCatalog::ReadTaskConfig`. It preserves skills without ready poses and reports missing or ambiguous literal group owners. Inventory entries are not execution permissions.

`TaskContext::Query` returns available, unavailable or unknown, with all reasons. Explicit inputs override context values and defaults for that query only. Context values retain their owner. Removing one context restores earlier contributions; repeated removal does nothing. Querying does not execute official predicates or change the game.

Target, spatial-anchor and visual-choice requirements use the visual scene's resolved input view. Target presence is checked against owned entity generations and world identity without engine reads. The caller cannot supply that availability. Inputs are rechecked when a queued task starts, then captured for its branches and resource scope. Resource loading and visual completion remain separate observations.

## Execution

Sequences contain submissions, request waits, second/frame waits, serial/parallel groups, branches and finite or finish-requested loops. Leaf operations are animation commands or existing `VisualInstruction` values.

Create an execution with a unique token, immutable sequence and `TaskSequenceServices`. Call `Advance(delta, frame)` once per engine frame. Sequential waits carry unused time; parallel branches advance independently. Cooperative yields retain cursors and time credits. An asynchronous completion without a timestamp does not supply inferred remaining frame time.

The kernel allocates a request serial and passes it with the execution token to `submit`. The adapter resolves owned participants. An unsubmitted request may await entity creation or model binding with generation zero; the first resolved generations cannot change later. Include synchronous callbacks in the initial state; later callbacks use `Observe`. Submission, request completion, performance completion and drain are separate observations. Progress is monotonic within one receipt. An error observation stops the execution and records the request identity.

`RequestFinish` lets the current iteration finish before its successor. `Stop(Interrupted/Replaced)` runs the configured interruption branch. Actor loss, world exit and deletion skip that branch. Normal finish may retain natural tails; later deletion can cancel them. Each receipt receives at most one normal-finish and one cancellation notification.

Keep the execution and service providers alive until `Drained()`. Destruction performs no engine cleanup. Failed or partially submitted resources remain the adapter's responsibility until their drain observation arrives.

Reusing a request name starts a new serial after the prior performance finishes. The old request receives normal finish; once drained and no longer referenced by a cursor, it is discarded. Continuous loops do not retain a history of every completed request.

## Presentation adapter

Create `TaskPresentation` after the owned actor's model/domain handoff is ready. Supply its `VisualProgramPlayer`, resolved parameters and an animation resolver. `BindTaskAnimation` binds an existing `ActionPlayback`; `partner:<key>` resolves a declared partner through that visual scene. Unknown participants fail; a declared participant under construction can wait.

Service animation players on their normal owner frame, then call `TaskPresentation::Service(delta, frame)`. Keep the task, scene and bound players alive until the task's `Drained()` returns true. Actor deletion and world departure also close the visual scene through its owner lifecycle.

| Operation | Backend | Completion observation |
|---|---|---|
| Actor body animation | Existing FreeStyle playback | Current/next state and normalized time |
| Other animation layer | Animator play or cross-fade | That layer's state and time |
| Partner body animation | Existing partner idle-animation service | Partner's own Animator state and time |
| Visual instruction | `VisualProgramPlayer` instruction executor | Instruction acceptance/application |

Animation requests do not select an automatic successor. A completed non-loop state or observed exit completes its wait; looping states remain active. Releasing a receipt stops observation, not the Animator. The next task command owns the next state. Controlled domain changes cancel old requests, while observed Animator changes invalidate their bindings.

Use a performance wait for animation completion. A progress wait requires that exact threshold to have been observed; if the request finishes below it, execution reports an unsatisfied dependency instead of waiting forever or assuming the missed cue ran.

A later animation on the same participant and layer supersedes this execution's earlier observation. Its last progress is retained and `superseded` is reported separately from natural completion. Already-satisfied waits remain valid; unmet waits fail. External interruption still reports an execution error. Different participants and layers remain independent.

Submissions are asynchronous. Add explicit request waits for dependencies, including before reusing a request name. A visual creation instruction completing means the creation command was accepted, not that its effect finished. Effect controls retain and retry their exact pending receipts.

Each task gets a resource scope using the existing effects, partner and Timeline owners. Natural completion can retain tails; cancellation ends that scope. Named contexts outlive individual tasks until explicitly exited or the scene closes. `TaskPresentation::Drained()` includes scope cleanup even after the sequence kernel has finished. Old scope cleanup cannot detach a newer scope's event producer.

Visual instructions use their execution's resource scope unless `context` names a persistent context. Explicit `"context": null` selects the execution scope. Effects intended to finish naturally after a context exits must belong to a separate scope.

## Selection

`TaskController` owns one catalog on an already-prepared actor/domain. `Select` queries the requested task and optional slot override without native calls. An unavailable selection leaves the running task unchanged. The latest queued selection wins; an interruption already in progress is allowed to finish before it starts.

The controller waits for old producer work to be accepted or cancelled, not for independent visual tails to disappear. Final commands in a completed interruption branch are delivered before control passes. Natural completion uses only the configured `next` task; it does not infer StandBy. Successors are re-queried against the current context and at most one starts per frame.

Visual contexts contribute values, phase, slot replacements and disables only after entry completes. Exit revokes those contributions. Precedence follows actual entry order. Removing a context restores earlier owners; no official combat values are changed.

`Reset` cancels execution and leaves the visual scene for a domain/action change. `Close` also permanently closes the scene. Keep servicing until drained. A later task can reuse a reset controller.

The character service coordinates model preparation before starting a task. It restores any previous gaze binding through the ordinary domain-handoff path, then prepares the catalog's model domain. No preceding idle/action command is required. Re-selection replaces pending intent; cancel removes it; finish can be queued during gaze preparation. World departure or a battle frame cancels unstarted task intent. Preparation errors are published separately from execution errors.

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
| `visual` | `request`, `instruction` using the existing visual instruction schema |
| `wait_request` | `request`; optional `signal`: `submitted`, `accepted`, `performance` (default), `progress`, `drained`; `progress` requires its numeric value |
| `wait_seconds` | `seconds` |
| `wait_frame` | No additional fields |
| `repeat` | `child`; `count` and/or `until_finish` |
| `branch` | `condition`, `yes`, `no` |

Task fields: `sources` (`document`/`field`), `inputs`, `requires`, `tags`, `entry`, and `next` (`task` plus optional `inputs`). Input definitions use `type` (`bool`, `integer`, `number`, `text`) and optional `default`. JSON integer and floating-point types are distinct. Requirements use `op`: `value` (`name`, `is`), `phase` (`is`), `participant` (`name`), `target` (`name`), `anchor` (`name`), `choice` (`name`, `is`), or `unresolved` (`source`, `detail`).

Top-level `contexts` binds visual-context names to `values`, `phase`, `slots` (`slot`/`task`) and `disables` (one of `task`, `slot`, `tag`, plus `reason`). Task references are local to the catalog. Unsupported fields and broken references are reported during loading.

## Development control

Both builds read `VISUAL_PROGRAMS.json` beside the Framework DLL on startup. Its `programs` array accepts `file`, `targets`, `choices` and `anchors`. Visual programs register source defaults; task catalogs register by catalog ID and do not change those defaults. Files are parsed on the worker before runtime initialization.

HTTP `create` and development `create_prepared` accept `task_catalog`. The catalog source and model domain must match the prepared character. Set `transient: true` for a session-only preview. An omitted `transient` creates a layout-persistent instance. One configured visual scene owns its resources; replacing a live resource catalog is not supported.

`reload_tasks` rereads this manifest and its files on the worker, then publishes the task catalogs through the application queue. Its reply contains `task_catalog_revision`. Invalid or missing input leaves the registry unchanged; an explicit empty list removes the published candidates. Source-default visual programs are not replaced by this command.

Existing previews retain their original definitions and resources. Subsequent `create_prepared` requests use the new publication; snapshots identify their `task_catalog` and `task_catalog_revision`. Delete an old preview and create another to exercise changed definitions without restarting the game. Publication does not create, rebuild or play characters.

Create a preview with `create_prepared`, a prepared `source_request`, `task_catalog` and `transient: true`. Selecting a task prepares its model domain through the existing handoff.

Session-only instances appear in snapshots with `transient: true`, but are omitted from automatic saves and named layouts. Loading a layout leaves them untouched. Source replacement preserves their lifetime. Saved actors cannot retain references to session-only targets in gaze, tasks or visual contexts. Delete previews through the ordinary character lifecycle.

The existing development command envelope accepts:

- `task_query`: return current execution, context values and task availability; optional `catalog`/`task` queries one task.
- `task_select`: requires `catalog` and `task`; accepts `inputs`, `slot`, `targets`, `choices`, `anchors`.
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
