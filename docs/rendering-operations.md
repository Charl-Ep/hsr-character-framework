# Rendering operations

Visual instructions support these rendering-only operations:

```json
{"op":"render.mask","node":"Weapon","mask":64,"enabled":true}
```

`node` is optional: omit it to select the actor's root hierarchy. Named nodes use
the official attachment map. `mask` is a nonzero 32-bit unsigned mask.

```json
{"op":"render.outline","add":[],"remove":["Weapon"]}
```

`add` and `remove` contain names from the official renderer cache. `reset` clears
the currently composed extra-target lists before applying these entries.

```json
{"op":"render.visibility","key":"effect.hide","visible":false}
```

`false` adds a private hide key; `true` removes the same key within the same
execution or named context. Visibility affects rendering only; collision,
triggers and sound retain their own state.

All three operations accept `recipient`, defaulting to `Caster`. A recipient must
resolve to an explicitly bound owned actor in the same world. Missing components
or named resources skip the instruction. Native failures remain errors.

The runtime composes overlapping mask and outline contributions across source
scenes. Execution/context exit withdraws its contributions and hide keys. Model
replacement retires old renderer identities. An external slot change is not
overwritten during withdrawal; uncertain partial writes retain their cleanup
obligation and report a failure.
