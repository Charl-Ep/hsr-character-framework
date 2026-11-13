# Visual scene runtime

Script fields and examples: [Script API](script-api.md).
Sequence execution: [Task runtime](task-runtime.md).

`VisualProgram` is the immutable visual part of a compiled script.
`VisualProgramBindings` supplies live actor identity, resolvers and input
providers. `VisualProgramRuntime` assembles the observers and resource services;
`VisualProgramPlayer` owns scene execution and resource retirement.

## Resource declarations and loading

Resource keys identify reusable definitions.
Instruction order determines use. One definition can create multiple requests
within an owning action or state.

Effects load when created. Partners load when their creation instruction runs.
Projectile parameter sources are resolved when first used by a firing group.
Entry and restoration flows load resources as their instructions execute.

`VisualEffectContext` holds submitted requests through pending loads, native
execution and release. Controls retain the exact request identities they have
already visited. Removal of a resource key selects matching requests within the
current owner.

Effect mutex rules distinguish creation, replay and suppression. Replay can
adopt only an existing request under the same owner and effect key. Suppression
completes the instruction with zero new receipts. Native animation-event effects
are recorded separately and can be retired by their actor-owned requests.

## Cued animation execution

An `animation.cued` action uses the body's actual animation clock while retaining
independent visual tracks. Selection first prepares its resource instructions.
Only after preparation succeeds does the caller start body playback and bind
its playback/model receipt with `Played`.

`Observe` advances cues from the matching state's normalized progress. Progress
is monotonic within a receipt. A pending instruction pauses its own track while
other tracks continue.

After all tracks finish and the completion boundary is reached, a declared
successor is played or observed. Its tail instructions wait for that successor's
actual progress.

Restarting a cued action replaces the default action resource scope. A
continuation can retain that scope's resources and variables. Named state
scopes remain independent. Actions retain distinct identities when they use
the same body state.

## State scopes and native events

Each entered state has its own resource owner and token. Its entry, exit,
restoration and callback execution use captured input values. Re-entry after
exit creates a new token; callbacks retain their original owner.

Phase configuration comes from declared official configuration sources.
The latest active phase owner takes precedence;
withdrawal exposes the previous live owner or the base idle. Value contributions
and resource ownership remain separate from the current body animation.

Native event routes are declared before model event construction. The runtime
attaches the actor's actual event receiver before playback. Detaching prevents
new associations; an executing native sequence retains its original owner for
late submissions and cleanup.

Signals are queued for an action or state token. Action changes discard stale
action signals; state-scoped signals survive body selections until their state
exits. Pending signal instructions retain their cursor and run on the owner
frame.

## Resource backends

| Backend | Ownership and completion |
|---|---|
| Effects | `VisualEffectContext` retains requests until the official manager releases them |
| Partners | `VisualPartners` services SDK sessions on world frames; parent retirement cleans up owned children |
| Timelines | `VisualTimelines` retains native registry borrows through cancellation and cleanup |
| Movement | Shared motion leases serialize writers to the same owned entity channel |
| Model/render contributions | The corresponding service records the writer and withdraws its own contributions |

Partner controls wait for model loading. Independent creates can precede their
controls to load concurrently. Animation-follow tracks a target; explicit
partner animation uses the partner's own player.

Timeline requests marked `shared_environment` use shared admission within the
Framework. Cancellation retains occupancy until the native playback receipt
retires. Admission is scoped to Framework requests; external native playback
and delayed global environment recovery remain under game control.

Renderer masks, outline contributions, hide keys, scale contributions and timing
requests withdraw with their owning scope. Model replacement retires old
component identities before rebinding. A model configuration setter or explicit
skill model change requires an explicit reset/change instruction to restore
its values.

Skill model changes use the official synchronous operation. Cold resources can
stall that call. A declined or unchanged operation is skipped. This changes the
model within the current presentation environment. Exploration/battle model
selection uses the character's domain handoff.

## Completion and cleanup

Natural action completion can retain effect and sound tails. Interruption ends
the action owner; explicit state exit runs its exit flow and retires that state.
Whole-scene shutdown cancels remaining owners and drains their resources.

`Leave` retires model presentation; `End` closes the player. Continue `Service`
until `Drained`, including effects, native producers, partners and Timelines.
`Service(false)` pauses instructions and continues lifetime collection during a
body handoff. Release the player after engine cleanup completes.

## Reconstruction

`Capture` returns value-only scene and state snapshots in entry order, preserving
phase precedence. `Restore` queues reconstruction after the destination model
and event receiver are ready. Target IDs are remapped before creation; captured
positions and choices supply the reconstruction inputs.

Restore recipes rebuild resources directly.
Declared live effect keys preserve creation order and multiplicity but restart
their playback. Native requests, component generations and instruction progress
remain runtime-only data.

Each live persistent scope needs a reconstruction recipe. An empty recipe is
valid and differs from having no recipe. Missing recipes or changing owners
report a layout error and preserve the prior save. Cancellation drains
partially reconstructed resources through the normal cleanup path.
