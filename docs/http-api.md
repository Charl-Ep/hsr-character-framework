# HTTP and SSE API

HTTP submits commands to the same application service as the menu. The contract is [openapi.json](openapi.json). Stopping the listener closes connections but preserves characters and accepted commands.

## Enable the listener

Create `http-settings.cfg` beside `Hksr.CharacterFramework.dll`:

```ini
[http]
enabled=false
address=127.0.0.1
port=39270
token=
origins=
```

To enable, set `enabled=true`, a literal IPv4/IPv6 address, port 1–65535 and a random token of 32–256 printable ASCII characters without spaces. A missing or invalid file leaves HTTP disabled. The worker checks changes every 250 ms and restarts the listener when settings change; bind failures wait for another configuration change.

HTTP is unencrypted. Use loopback or a trusted LAN interface with appropriate firewall rules. Keep tokens out of source control.

## Authentication

Requests require `Authorization: Bearer <token>`. Query-string tokens are rejected. An `Origin` must exactly match a comma-separated entry in `origins`; wildcard and null origins are rejected.

CORS preflight permits GET, POST, Authorization, Content-Type and Last-Event-ID. Browser SSE clients use streaming `fetch` to supply the authorization header.

## Commands

Send a JSON object to `POST /v1/commands` with `Content-Type: application/json`.

HTTP 202 returns `serial` and `request_url`. Poll that URL for queued/dispatched and the application reply. `accepted` is dispatch acceptance; asynchronous completion is reported by `operation` in snapshots/events. Query-only commands use operation 0. See [operation semantics](application-services.md#操作结果与历史).

| op | Parameters |
|---|---|
| `catalog` | Optional `refresh` |
| `prepare_source` | `source` |
| `retry_source`, `release_source` | `source_request` |
| `create` | `source_request`; optional `scene`, `placement`, `near_player`, `player_world_offset` |
| `replace_source` | `character`, `source_request` |
| `enable` | `character`, `enabled` |
| `delete`, `rebuild`, `idle` | `character` |
| `browse_actions`, `browse_npc_actions` | `character` |
| `action` | `character`, `action`; `revision` for configured, ready and visual-behavior actions |
| `place` | `character`, `placement`; optional `relative` |
| `look` | `character`, `mode`; target/point where required, optional `max_distance` |
| `layout` | `operation`; optional `name`, `overwrite`, `scene` |

Disabling removes the live entity but retains its logical instance; deleting drains resources and removes the instance. `idle` returns to the current phase's battle idle.

NPC source: `{"kind":"npc","npc_id":"1110"}`. Player source: `{"kind":"player","player_id":"1217","enhanced":0,"skin":0}`.

Player sources can specify an explicit `npc_id`. Use catalog values; ambiguous preparation returns candidates.

64-bit IDs and sequences are decimal strings in responses. Requests also accept nonnegative JSON integers up to 2^53−1. Unknown fields or invalid types return 400.

POST is not idempotent. After a lost response, inspect state before retrying. The listener retains pending receipts and 256 completed receipts; expired request URLs return 404. Listener restart clears receipt history, while application restart creates a new event stream.

## Placement and gaze

Placement contains `position:{x,y,z}` and `rotation:{x,y,z,w}`. Values must be finite; quaternion norm-squared tolerance is 0.001. Relative rotations multiply on the left. Snapshot placement is saved intent; controls report observed pose and pending writes.

For creation, omitted placement means origin and identity rotation. `near_player=true` takes precedence and uses the player's pose plus a world offset, defaulting to `{x:2,y:0,z:0}`. An omitted scene uses the map current at dispatch.

`snap_to_ground=false` disables grounding/terrain correction while preserving animation/navigation deltas. True restores the session's original movement mode; omission preserves policy. To snap in place, submit relative zero translation, identity rotation and true. This setting does not remove colliders.

Gaze modes are `automatic`, `disabled`, `player`, `instance` and `point`. Instance targets must be on the same map. `max_distance` is nonnegative; zero adds no distance limit. Out-of-range gaze stops and resumes when the target returns. Official angle/IK constraints still apply.

Controls expose locale-independent phases and errors. `gaze_phase=restoring` means official fade-out is pending.

## Actions

Configured actions use `{"kind":"configured","domain":"battle","state":"Skill01"}`; domain can also be `exploration`. Ready actions use the catalog's `source` and `pose`, with associated attacks provided in the same view. Both require the current catalog revision.

NPC motions use `{"kind":"npc","id":"123"}` without a revision. Action completion is separate from the initial returned playback call.

Shared reactions use `{"kind":"visual_behavior","source":"<character config>","flag":28,"recover":false}` from the `reactions` array, with the current catalog revision. False enters and holds the configured body/expression; true plays recovery and releases the expression. Identity is the character configuration plus official flag, not the animation name. Layouts save that identity and resolve it again on restoration.

`browse_actions` requests reaction data for that character. Restoring a saved reaction also requests its source; ordinary creation and layout restoration do not. The result is cached by source for the process. Reaction availability checks the resolved configuration; playback checks the active Animator. The implemented scope is body phases, expression and blink. Material, speed and floating effects are not implemented; entries without animation remain listed with an unavailable reason. A resolved reaction does not wait for unrelated skill discovery.

Authored program actions use `{"kind":"program","program":"scene","action":"enter","choices":{"variant":"b"}}` without a catalog revision. Use an entry from the action response's `program` array; `choices` is optional. The bound definition supplies the body state. A different program ID or unknown action key is rejected by the character service.

## Map layouts

The working layout is saved in `Layouts/map-<escaped-key>/current.json`; named snapshots use `snapshots/layout-<name>.json`. Layouts retain official sources, placement, enable state, gaze, selected actions and presentation phases. Restored actions start from the beginning.

`layout` operations:

| Operation | Behavior |
|---|---|
| `refresh` | Read snapshot names |
| `save` | Save a named snapshot; existing names require `overwrite=true` |
| `load` | Prepare sources/actions, then replace this map's instances |
| `delete` | Delete the named snapshot |
| `retry_current` | Retry initial restoration or save current state after initialization |
| `keep_current` | Abandon failed restoration and overwrite the working save |

Save/load/delete require a name. Supplying `scene` rejects dispatch after a map change; omission targets the map current at dispatch.

Layout replies have operation 0. Read `runtime.layouts.maps[scene]`: command, operation, phase, busy, error, storage_error, snapshots, revision and saved_revision. Phases are io, reading, preparing, removing, applying, ready, failed or cancelled. Applying includes pending character creation; save/delete reach ready after filesystem completion.

State capture and background writes each run at a 250 ms cadence. Unchanged values are not rewritten. Write failures remain visible until an explicit retry; failed initial restoration preserves the file. `keep_current` can overwrite it with an empty layout.

Instance gaze references are remapped on restoration. Cross-map targets, external packages and independent development animation layers are not supported by layout persistence.

## Queries and events

| Endpoint | Result |
|---|---|
| `GET /v1/snapshot` | Current snapshot and atomic event cursor |
| `GET /v1/catalog` | Official rows and source preparations |
| `GET /v1/characters/{id}/actions` | Configured, ready/follow-up, reactions, programs and queried NPC actions |
| `GET /v1/requests/{serial}` | Dispatch receipt |
| `GET /v1/events` | SSE stream |

GET reads published data. Request catalog/action queries through POST first; lists may be partial while `finished=false`.

Action views expose `domain_ready.exploration` and `domain_ready.battle`. Configured and ready actions are selectable after their domain is ready, including exit-effect data. The other domain may still be loading.

Cursors use `<32 lowercase hex stream characters>:<decimal sequence>`. Connect without Last-Event-ID for a reset containing a full snapshot. Reconnect with the last applied event ID. Invalid syntax returns 400; expired, future or foreign-stream cursors produce reset.

Updates contain changed character/operation maps, with null indicating deletion or history expiry. Runtime and sources appear when changed; catalog_changed and actions_changed invalidate larger views. Apply the whole event before saving its cursor. Intermediate changes may be coalesced.

Idle streams receive a comment every 15 seconds. Application closure ends streams after the final change; listener reconfiguration interrupts connections immediately.

## Limits

| Resource | Limit |
|---|---|
| Concurrent SSE streams | 8 |
| Worker slots reserved for ordinary requests | 4 |
| Queued connections | 64 |
| Request body | 64 KiB |
| Commands awaiting dispatch | 256 |
| Completed dispatch receipts | 256 |

Capacity failures return 429. Read/write timeouts are 2 seconds; keepalive timeout is 1 second. Listener stop interrupts active sockets. Network workers use snapshots and never call game methods.
