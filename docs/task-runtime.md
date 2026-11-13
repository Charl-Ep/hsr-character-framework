# Task runtime

Script fields and examples: [Script API](script-api.md).

The runtime compiles script documents into immutable plans and executes them on
owned additional actors. Menu, HTTP and development controls use the same
application queue. Scripts combine animation, sound, effects, movement and
state changes using explicit inputs.

## Compilation and loading

`ReadScriptLibrary` discovers `*.script.json` recursively under `Scripts` beside
the DLL. `ParseScript` reads one document and returns its prepared catalog,
visual resources, default selection and input defaults. Declared resources are
loaded when their creation instructions execute.

Definition modules are resolved before typed compilation. Imports supply named
resources, states, functions and action templates; actor-local overrides are
applied before validation. The directory reader caches module text within one
load. Each actor owns its runtime requests.

`switch` compiles first-match branches over captured action inputs.
`if` evaluates mutable context predicates.

One source candidate is selected automatically unless marked `default: false`.
Multiple candidates require one explicit default or must all be explicitly
non-default. Script IDs are unique within the loaded directory; conflicting
defaults are errors.

The compiler places ordinary `run` actions in `TaskCatalog::tasks` and
`animation.cued` actions in `VisualProgram::actions`. They share the script ID,
resources and declared states. `TaskSequenceExecution` executes ordinary flows;
the [scene player](visual-programs.md) evaluates cued animation tracks. The
Framework prepares the battle model environment before either kind starts.

| Type | Responsibility |
|---|---|
| `TaskSequenceInput`, `TaskCatalogInput` | Editable programmatic inputs |
| `TaskSequence`, `TaskCatalog` | Validated immutable plans and prepared dependencies |
| `TaskSequenceExecution` | Cursors, waits, request observations and exit reason |
| `TaskPresentation` | Connects one execution to animation and resource services |
| `TaskController` | Selection, input queue, successors and persistent state contributions |
| `TaskLifetimes` | State callbacks and their independent execution lifetimes |
| `CharacterTasks` | Shared action directory for menu and HTTP |

Construction validates static references once. Playback reuses the prepared plans.

## Flow execution

`TaskSequenceExecution::Advance(delta, frame)` advances one execution on the
owning engine thread. Arrays submit operations in order. Explicit waits consume
submission, acceptance, start, progress, completion or drain observations.

Sequential second waits preserve unused frame time. Parallel branches advance
independently and join when their flows finish. Remaining frame time is derived
from timestamped observations. Cooperative yields retain cursors and time credits.

`call` reuses a function in the caller's request scope. `start` launches an
independent child invocation; its requests can reference the calling scope.
An already-running identity reuses its execution. `join` waits for registered
children. A finished invocation can run again with a new identity.

Finish requests allow the current `until_finish` loop iteration to end normally.
Interruption or replacement runs the authored interruption flow. Actor loss,
world departure and deletion skip work that requires a live actor. An error
stops the affected execution and records the failing request.

A skipped step produces a skipped receipt. Waits consume that outcome;
success continuations run on successful completion. Native failures produce errors.

## Requests and ownership

Each submission receives an execution token and request serial. Its first
resolved actor and model generations become part of that receipt. Synchronous
observations are returned with submission; later callbacks use `Observe`.
Observations are matched to their execution token and model generation.

Acceptance, performance completion and resource drain are separate facts. A
finished flow may retain sound, effect or movement tails. Cancellation requests
cleanup, but the execution and its service providers stay alive until
`Drained()`. Complete engine cleanup before releasing those objects.

Reusing a request name creates a new serial after the preceding performance has
finished. Old receipts are discarded after resource drain and cursor release.

## Animation, sound and movement

Body animation uses the owned FreeStyle player; other layers use the appropriate
Animator layer. Partners use their own animation service. Progress is read from
the exact playback/model receipt.

`animation_end`, clip completion, animation-request completion and state departure
are distinct observations. A progress wait fails if the request ends below its
threshold. A later request on the same participant and layer supersedes earlier
observation. Releasing an animation receipt ends observation while the Animator
continues playback.

Sounds use the actor's emitter and separate native start/end callbacks. Natural
completion permits a sound tail; cancellation selects the owned request by
callback and emitter identity. Automatic voice supplements use qualified native
cues and remain separate from explicit script sound commands.

Movement binds a base-layer animation request or submits an instant placement.
The SDK evaluates the movement and owns its native host lease. A later explicit
placement supersedes earlier scripted movement, including its return-to-origin
steps; body animation continues. Model replacement and cancellation drain the
owned movement before rebinding.

Actor playback rate affects its animation clock, authored second waits, timed
hit expressions and qualified sound selection. Audio already playing retains
its playback rate; the shared game clock remains under game control.

## Selection and state

Replacement actions own the main performance. Input actions use an independent
ordered queue and leave that selection intact. Unavailable selections leave
the running action in place. The latest pending replacement wins after any
current interruption flow completes.

Natural completion follows the explicit `next` action. The successor is queried
against current state. At most one successor starts per frame. Independent
inputs preserve the saved action.

Input values and visual bindings are captured when execution starts. Conditions
that name a state read that live state. `for_each` creates per-member request and
target namespaces from the supplied collection. Explicit empty collections remain
empty; missing entity targets remain unavailable.

Declared states own variables, numeric bounds, phase configuration, receiver
properties and optional callbacks. Contributions become visible at their
configured activation boundary and withdraw on exit. Removing one contribution
exposes earlier live owners. Logical values can be used independently of
visual resources.

Playback tracking is prepared centrally from an action's `runtime` declaration.
Its selected/performance values track the script lifecycle. Persistent character
states have their own declared values and lifetimes.

State callbacks retain their resource owner, participant generations and
captured inputs across ordinary action changes. Repeated state instances use
their declared matching policy. Model/controller invalidation retires obsolete
bindings.

## Hits and visual notifications

`hit` delivers a visual hit to an explicitly owned receiver, or plays a spatial
impact at a supplied point. Receiver animation, expression and effects use the
receiver's configuration and active state contributions. Delivered reactions
and effects belong to the receiver and can outlive the attack. Point impacts
play spatial effects.

State event subscriptions are visual notifications. Scripts choose hit and
attack boundaries explicitly. Attack history groups receivers for those notifications.

A manually selected shared reaction has its own actor-local contribution.
Accepted ordinary body, ready-pose or movement playback clears that contribution;
matching recovery also clears it. Persistent skill states and their resource
lifetimes are separate.

## Publication and persistence

Development `reload_tasks` reads the script directory on the worker and publishes
the complete prepared registry through the application queue. A missing or
invalid directory leaves the current registry unchanged. An existing empty
directory publishes an empty registry. Publication atomically rebuilds source
defaults and advances the catalog revision.

Existing actors retain their original definitions and resources. New actors use
the current publication. GUI, HTTP and development commands share typed
action identities.

Layouts store script/action identity, inputs, declared state values and logical
target references. A configured successor replaces the saved selection after
handoff. Cancellation clears it. Reconstruction remaps target identities and
restores resources before restarting the selected action from its entry.
Native objects, receipts, cursors and playback clocks are runtime-only data.

Persistent visual resources require explicit reconstruction recipes. Empty and
absent recipes are distinct. Invalid restoration reports an error and keeps
the previous valid layout. Session-only previews are excluded from persistence.
