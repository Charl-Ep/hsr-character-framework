# Task runtime

The `tasks` module provides typed visual tasks, configuration loading, owned-resource execution and per-actor selection. Development commands exercise it through the application queue. User-facing task menus, HTTP endpoints and task-layout persistence are pending.

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

Submissions are asynchronous. Add explicit request waits for dependencies, including before reusing a request name. A visual creation instruction completing means the creation command was accepted, not that its effect finished. Effect controls retain and retry their exact pending receipts.

Each task gets a resource scope using the existing effects, partner and Timeline owners. Natural completion can retain tails; cancellation ends that scope. Named contexts outlive individual tasks until explicitly exited or the scene closes. `TaskPresentation::Drained()` includes scope cleanup even after the sequence kernel has finished. Old scope cleanup cannot detach a newer scope's event producer.

## Selection

`TaskController` owns one catalog on an already-prepared actor/domain. `Select` queries the requested task and optional slot override without native calls. An unavailable selection leaves the running task unchanged. The latest queued selection wins; an interruption already in progress is allowed to finish before it starts.

The controller waits for old producer work to be accepted or cancelled, not for independent visual tails to disappear. Final commands in a completed interruption branch are delivered before control passes. Natural completion uses only the configured `next` task; it does not infer StandBy. Successors are re-queried against the current context and at most one starts per frame.

Visual contexts contribute values, phase, slot replacements and disables only after entry completes. Exit revokes those contributions. Precedence follows actual entry order. Removing a context restores earlier owners; no official combat values are changed.

`Reset` cancels execution and leaves the visual scene for a domain/action change. `Close` also permanently closes the scene. Keep servicing until drained. A later task can reuse a reset controller.

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

Task fields: `sources` (`document`/`field`), `inputs`, `requires`, `tags`, `entry`, and `next` (`task` plus optional `inputs`). Input definitions use `type` (`bool`, `integer`, `number`, `text`) and optional `default`. JSON integer and floating-point types are distinct. Requirements use `op`: `value` (`name`, `is`), `phase` (`is`), `participant` (`name`), or `unresolved` (`source`, `detail`).

Top-level `contexts` binds visual-context names to `values`, `phase`, `slots` (`slot`/`task`) and `disables` (one of `task`, `slot`, `tag`, plus `reason`). Task references are local to the catalog. Unsupported fields and broken references are reported during loading.

## Development control

In a development build, `VISUAL_PROGRAMS.json` accepts `file`, `targets`, `choices` and `anchors`. Visual programs register source defaults; task catalogs register by catalog ID and do not change those defaults.

Create a preview with `create_prepared`, a prepared `source_request`, and `task_catalog`. Its source and model domain must match the catalog. One configured visual scene owns its resources; replacing a live resource catalog is not supported. Prepare the catalog's model domain through the ordinary action control before selecting a task.

Task previews are session-only. Ordinary `create_prepared` also accepts `transient: true`. These instances appear in snapshots with `transient: true`, but are omitted from automatic saves and named layouts. Loading a layout leaves them untouched. Source replacement preserves their session-only lifetime. Saved actors cannot retain references to session-only targets in gaze or visual contexts. Delete previews through the ordinary character lifecycle.

The existing development command envelope accepts:

- `task_query`: return current execution, context values and task availability; optional `catalog`/`task` queries one task.
- `task_select`: requires `catalog` and `task`; accepts `inputs`, `slot`, `targets`, `choices`, `anchors`.
- `task_finish`: finish the selected continuous task at its configured boundary.
- `task_cancel`: run its interruption and cancel retiring tails.

All require `character` and `creation` from the current snapshot. Selection requires the catalog's model domain to be ready. `body` binds the owned actor, and `partner:<key>` binds a declared visual partner. Commands run through `ApplicationService` on the exploration-world frame; they cannot target official battle actors.

Query results include active and retiring request receipts: authored name/source, execution, participant, entity/model generations, submission, acceptance, performance completion, progress and drain. These are copied from existing observations only when queried; reading them does not query the engine or retain execution history.

Transport acceptance is separate from task availability, execution and drain. Full task selection and context persistence are not yet implemented.
