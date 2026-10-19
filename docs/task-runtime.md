# Task runtime

The `tasks` module provides typed visual tasks, an execution kernel and adapters for owned animation and visual resources. Application selection, configuration loading, persistence and frontend integration are pending.

## Model

| Type | Responsibility |
|---|---|
| `TaskSourceInventory` | Configured skills, entry/prepare references and skill-owned ability groups |
| `TaskDefinition` | Visual task identity, typed inputs, prerequisites and compiled-sequence key |
| `TaskContext` | Per-actor phases, values, slot overrides and disable contributions |
| `TaskSequenceExecution` | One execution's cursors, waits, request receipts and exit reason |
| `TaskPresentation` | A sequence bound to owned animation players and a visual resource scope |

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

Submissions are asynchronous. Add explicit request waits for dependencies, including before reusing a request name. A visual creation instruction completing means the creation command was accepted, not that its effect finished. Effect controls retain and retry their exact pending receipts.

Each task gets a resource scope using the existing effects, partner and Timeline owners. Natural completion can retain tails; cancellation ends that scope. Named contexts outlive individual tasks until explicitly exited or the scene closes. `TaskPresentation::Drained()` includes scope cleanup even after the sequence kernel has finished. Old scope cleanup cannot detach a newer scope's event producer.
