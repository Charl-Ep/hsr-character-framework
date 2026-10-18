# Task runtime

The `tasks` module provides a typed visual-task model and an engine-independent execution kernel. It is not yet connected to the application menu, HTTP or game playback adapters.

## Model

| Type | Responsibility |
|---|---|
| `TaskSourceInventory` | Configured skills, entry/prepare references and skill-owned ability groups |
| `TaskDefinition` | Visual task identity, typed inputs, prerequisites and compiled-sequence key |
| `TaskContext` | Per-actor phases, values, slot overrides and disable contributions |
| `TaskSequenceExecution` | One execution's cursors, waits, request receipts and exit reason |

`BuildTaskSources` consumes `OfficialCatalog::ReadTaskConfig`. It preserves skills without ready poses and reports missing or ambiguous literal group owners. Inventory entries are not execution permissions.

`TaskContext::Query` returns available, unavailable or unknown, with all reasons. Explicit inputs override context values and defaults for that query only. Context values retain their owner. Removing one context restores earlier contributions; repeated removal does nothing. Querying does not execute official predicates or change the game.

## Execution

Sequences contain submissions, request waits, second/frame waits, serial/parallel groups, branches and finite or finish-requested loops. Leaf operations are animation commands or existing `VisualInstruction` values.

Create an execution with a unique token, immutable sequence and `TaskSequenceServices`. Call `Advance(delta, frame)` once per engine frame. Sequential waits carry unused time; parallel branches advance independently. Cooperative yields retain cursors and time credits. An asynchronous completion without a timestamp does not supply inferred remaining frame time.

The kernel allocates a request serial and passes it with the execution token to `submit`. The adapter resolves participants through owned entity/partner bindings and returns that identity with the entity generation and participant. Queued work may have `submitted=false` and model generation zero; the first actual model observation binds the model generation, which cannot change later. Include synchronous callbacks in the initial state; later callbacks use `Observe`. Submission, request completion, performance completion and drain are separate observations. Progress is monotonic within one receipt.

`RequestFinish` lets the current iteration finish before its successor. `Stop(Interrupted/Replaced)` runs the configured interruption branch. Actor loss, world exit and deletion skip that branch. Normal finish may retain natural tails; later deletion can cancel them. Each receipt receives at most one normal-finish and one cancellation notification.

Keep the execution and service providers alive until `Drained()`. Destruction performs no engine cleanup. Failed or partially submitted resources remain the adapter's responsibility until their drain observation arrives.

Reusing a request name starts a new serial after the prior performance finishes. The old request receives normal finish; once drained and no longer referenced by a cursor, it is discarded. Continuous loops do not retain a history of every completed request.
