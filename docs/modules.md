# Framework modules

Headers use `hksr/framework/<module>`. Source and test directories follow the same responsibilities.

| Directory | Responsibility |
|---|---|
| `application` | Typed commands, receipts, snapshots and operation history |
| `catalog` | Source preparation, action discovery and display families |
| `character` | Logical instances, creation generations, controls and map membership |
| `appearance` | Private model assembly, overlays and retirement |
| `presentation` | Action selection, domain handoff, ready poses and phase rules |
| `assets` | Package, mesh, texture and material inputs |
| `layout` | Saved intent, restoration and filesystem storage |
| `serialization` | File codecs returning typed values |
| `host` | DLL lifecycle and SDK/frontend composition |
| `ui`, `http` | Menu and network frontends |
| `tools`, `src/development` | Development transport and observation bridge |

## Execution

Frontends submit `CharacterCommand` values to `ApplicationService`. `ApplicationCoordinator` dispatches a batch on the official frame. Character and presentation sessions call SDK capabilities; copied snapshots return to frontends.

`FrameworkHost` assembles services, `HostInspection` handles requested catalogs, and `HostPublication` publishes state. Logical instances survive map changes through new engine creation generations.

Creation, animation inputs, model transitions, controls and effect requests use SDK factories. Appearance consumers share `ModelServices` but retain separate root, asset-lease and native-object owners.

Player selections create an independent story actor from the selected prefab/configuration; NPC selections use map-NPC inputs. Catalog work and batches of new instances advance within elapsed-time slices. Individual engine calls are not preemptible.

## Visual effect ownership

`VisualEffectContext` owns explicitly submitted or adopted effect requests. It can persist across body actions. `End` requests cancellation; `Service` drains asynchronous results. Progress and Animator controls address retained requests, not global names.

`VisualEffectBindings` maps an SDK sequence execution to a selected context. Feed execution notifications to `Observe` and matching official effect submissions to `Track`. Sequence completion releases the binding without ending the context. The program owner controls the context's lifetime and keeps it alive through cleanup.

These are composition primitives; `FrameworkHost` does not install sequence observers or choose program bindings automatically.

## Data and builds

File codecs return `CharacterPackage`, `AssetManifest`, `SceneLayout` and typed presentation profiles. HTTP and development transports encode/decode at their boundaries. Runtime state uses structures, enums and variants.

Build targets are `CharacterContent`, `CharacterRuntime`, `CharacterUi` and `CharacterHttp`. `framework.module-boundaries` checks serialization dependencies, frontend isolation and direct engine-binding usage. Shared test fixtures are in `tests/support`.
