# Framework modules

Headers use `hksr/framework/<module>`. Source and test directories follow the same responsibilities.

| Directory | Responsibility |
|---|---|
| `application` | Typed commands, receipts, snapshots and operation history |
| `catalog` | Source preparation, action discovery and display families |
| `character` | Logical instances, creation generations, controls and map membership |
| `appearance` | Private model assembly, overlays and retirement |
| `presentation` | Actor playback and scoped audiovisual resources; groups below |
| `tasks` | Typed task catalogs, context queries, selection, execution and presentation adapters |
| `assets` | Package, mesh, texture and material inputs |
| `layout` | Saved intent, restoration and filesystem storage |
| `serialization` | File codecs returning typed values |
| `host` | DLL lifecycle and SDK/frontend composition |
| `ui`, `http` | Menu and network frontends |
| `tools`, `src/development` | Development transport and observation bridge |

## Presentation groups

Public contracts, implementations and tests use matching subdirectories.

| Group | Responsibility |
|---|---|
| `playback` | Body ownership, animation and pose requests, expression writes, task playback bindings |
| `reactions` | Hit requests, receiver inbox and reaction/effect policy |
| `effects` | Scoped effects, event attribution, effect layouts and spatial impacts |
| `scene` | Actor visual contexts, input capture, observer assembly, resource retirement and snapshots |
| `model` | Model/action handoff and scoped model/rendering contributions |
| `audio` | Owned voice requests and BGM synchronization scopes |
| `timing` | Actor-local clock contributions |
| `movement` | Owned entity movement and placement anchors |
| `partners` | Auxiliary actor construction, bindings and retirement |
| `projectiles` | Visual projectile batches and completion |
| `timeline` | Timeline playback and scope policy |

`ActionPlayback` coordinates playback across its `ActionPlayback*.cpp` files.
Each player or scope owns its request state. Private implementation headers stay
beside their source.

`catalog/ActionSelection` resolves selections from prepared catalogs.
`tasks/ContextValues` and `ContextNumbers` define script values. File-reading
declarations belong to `serialization`.

`scene/VisualProgram` defines visual assets and restoration validation.
`scene/VisualProgramPlayer` defines the concrete consumer, service bindings and
runtime status. Asset codecs, partner contracts and Timeline contracts use the
asset types. `VisualProgramSnapshot.cpp` validates saved values.

The [task runtime](task-runtime.md) owns execution order, waits, conditions and
persistent state. Scene owns actor resources and native playback bindings.
`TaskPresentation` connects task execution to these bindings.

## Execution

Frontends submit `CharacterCommand` values to `ApplicationService`. `ApplicationCoordinator` dispatches a batch on the official frame. Character and presentation sessions call SDK capabilities; copied snapshots return to frontends.

While a map layout is being replaced, the coordinator rejects edits to that map's persistent instances. Task queries, other maps and session-only previews remain available. The restriction ends when replacement completes or fails.

`FrameworkHost` assembles services, `HostInspection` handles requested catalogs, and `HostPublication` publishes state. The registry retains logical instances across map changes; entering a map creates new engine generations for that map's enabled instances. The menu presents the current map's entries.

Creation, animation inputs, model transitions, controls and effect requests use SDK factories. Appearance consumers share `ModelServices` but retain separate root, asset-lease and native-object owners.

Player selections create an independent story actor from the selected prefab/configuration; NPC selections use map-NPC inputs. Catalog work and batches of new instances advance within elapsed-time slices. Individual engine calls are not preemptible.

Catalog snapshots contain `SourceSummary` values. `CatalogBrowser` owns prepared
SDK resources, and each creation plan retains the resources it needs independently
of the source request.

`CharacterPlan` holds source selection and retained preparation resources.
`CharacterRestoreState` holds copied phase, task, visual state and playback rate.
The registry retains this state between creation epochs; each session consumes
its initial copy and owns its currently applied playback rate. Layouts serialize
the restore values alongside source and placement values.

`VisualProgramLibrary` stores definitions. `VisualProgramBindings` supplies the
runtime providers and resolved targets for one creation. `ControlSubmission`
carries mutable command completion; UI snapshots expose copied control values.

`ActionPlayback` observes execution and automatic successors. `ModelActionHandoff`
reports results for its request; `CharacterSession` correlates them with its
selection. `SceneCharacters` adopts settled successors as the current instance
intent. Layouts save that intent, including automatic returns to idle.

Accepted body/reaction operations report `PlaybackEffects` facts to the task
controller. Manual reactions and persistent script contexts have separate owners
that publish their current state to UI and persistence consumers.

## Visual effect ownership

`VisualInstruction` defines operation values. `TaskPresentation` executes them
through playback endpoints implemented by
`presentation/playback/TaskPlaybackBindings`.

Each visual scene owns `VisualBodyResources`: attachment visibility/scale,
model contributions, visual configuration, BGM synchronization and local/foreign
timing scopes. Scope release, retirement and model handoff use this owner.
Timing scopes drain before their timing provider. Owners service pending
resources through closure before destruction. Cross-actor contributions,
projectiles, partners and model-change receipts have separate owners.

`VisualSceneTransients` retains point-hit effects, projectile batches and pending
model-change requests through drain. It services their clocks and releases
participants during model handoff. `ActionPlayback` centralizes body takeover,
preserving the distinction between replacing an action and suspending it for a
body hit; additive hits and secondary layers do not take the body channel.

`VisualEffectContext` owns explicitly submitted or adopted effect requests. It can persist across body actions. `End` requests cancellation; `Service` drains asynchronous results. Progress and Animator controls address retained requests, not global names.

`StartEntity` supplies separate caster/target references and typed visual options. Its logical key belongs to the context, independently of the optional engine name. Requests enter the context before submission, so a synchronous callback can cancel them without transferring them to the next action.

`VisualEffectBindings` maps an SDK sequence execution to a selected context. Feed execution notifications to `Observe` and matching official effect submissions to `Track`. Sequence completion releases the binding without ending the context. The program owner controls the context's lifetime and keeps it alive through cleanup.

For registered scripts, `FrameworkHost` composes `VisualProgramRuntime`, which
installs the SDK sequence, event-construction and effect observers. Actor creation
binds each script and matching presentation profile to its own visual contexts.

`VisualEventRoutes` links final event definitions to those executions using the task collection actually passed to the engine factory. Authored rules select a file and complete state membership or behavior key. A shared collection with conflicting rules remains unattributed. Receiver and entity generation belong to the binding; the current menu selection does not.

## Data and builds

File codecs return `CharacterPackage`, `AssetManifest`, `SceneLayout` and typed presentation profiles. HTTP and development transports encode/decode at their boundaries. Runtime state uses structures, enums and variants.

Build targets are `CharacterContent`, `CharacterRuntime`, `CharacterUi` and `CharacterHttp`. `framework.module-boundaries` checks serialization dependencies, frontend isolation and direct engine-binding usage. Shared test fixtures are in `tests/support`.

`framework.kernel-headers` compiles task contracts without complete action or
visual player types. `framework.body-resources` tests pending retirement,
independent handoff cleanup and timing-provider dependencies.
