# Visual programs

`VisualProgramPlayer` runs an authored scene across body actions. `ActionPlayback::ConfigureVisualProgram` connects it to the animation player while the battle presentation domain is enabled. The application supplies the definition and engine services; the default host does not load visual programs.

## Data

`ParseVisualProgram` reads the `hksr.visual-program` format.

| Field | Content |
|---|---|
| `id`, `source`, `scope` | Program identity, character configuration and scene name |
| `effects` | Local keys mapped to resource paths, explicit targets and entity-effect options |
| `actions` | Body state, entry instructions, progress lanes and completion destination |

Each instruction has an `op` and a `source` identifying its configuration node. Operations are `create`, `assign`, `progress`, `remove`, `trigger`, `timeline_play` and `timeline_stop`. An optional `when` selects an input by `choice` and `is`.

`progress.value` is a number or `{"variable":"name"}`. Each lane contains ordered cues with `at` and `instructions`. Cue times use normalized body progress; effect transition durations use seconds. Lanes advance independently.

An action with `restart: true` creates a new scene. Other actions retain its effects and variables. `completion_state` requests a successor after `complete_at` and all lanes finish. Without it, the scene remains available for another action. Local keys do not set the engine's `UniqueEffectName`; `unique_name` is an explicit option.

## Services and ownership

`VisualProgramServices::create` resolves targets, retains inputs and calls `VisualEffectContext::StartEntity`. Trigger, immediate-fade and Timeline operations use supplied services. Timeline submission requires cancellation and drain observations.

`Select` queues intent. `Service` prepares the scene and collects independent input errors. The body starts after preparation succeeds; `Played` and `Observe` identify its actual playback and model. Pending controls retry only unresolved receipts, without restarting accepted transitions.

`Leave` retires the scene and permits a later restart. `End` closes the player. Continue `Service` until `Drained`, including effects, executing native producers and Timeline cleanup.

For native events, declare `VisualEventRoutes` selectors before model event construction, then attach the current receiver and scene before playback. Definitions are reusable. Detaching prevents new associations; executing sequences keep their original cleanup responsibility until completion or reset.
