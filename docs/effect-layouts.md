# Effect layout instructions

Layout instructions operate on effect keys in the current visual context.

```json
[
  {"op":"effect.layout","effect":"layout","command":"hide"},
  {"op":"effect.layout","effect":"layout","command":"show","fade":true},
  {"op":"effect.layout","effect":"layout","command":"perform","perform":0},
  {"op":"effect.layout","effect":"layout","command":"transfer","target":"next_layout","fade":false}
]
```

Perform values are 0 AddImmediately, 1 RemoveImmediately, 2 SwitchPerform and
3 MaxHighlightPerform. `fade` applies only to show and transfer.

Each key must already have an owned, submitted effect receipt. Transfer requires
exactly one receipt per key and fixes that pair before waiting for loading.
Hide, show and perform apply to each matching receipt. Missing plugins skip the instruction; loading waits;
native failures remain errors.

Effect receipts retain their existing context ownership. Context removal drains
them through the owned-effect lifecycle. Transfer preserves native source/target
inheritance. Reversal requires an explicit transfer command.
