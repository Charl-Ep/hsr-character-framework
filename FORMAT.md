# Character package format

`ParseCharacterPackage` reads a `character.json` descriptor into `CharacterPackage`.

```json
{
  "format": "hksr.character-package",
  "schema": 1,
  "id": "example",
  "display_name": "Example",
  "source": {
    "kind": "official",
    "npc_id": 1110
  }
}
```

| Field | Requirement |
|---|---|
| `id`, `display_name` | Nonempty strings |
| `source.kind` | `official` or `external` |
| `source.npc_id` | Integer in 1–4294967295 |
| `source.appearance` | Optional for official sources; required for external sources |

## Appearance

`source.appearance` contains:

| Field | Requirement |
|---|---|
| `template_prefab` | Nonempty official prefab path |
| `character_config` | Nonempty official character configuration path |
| `weapon_config` | Optional string |
| `asset_directory` | Required package-relative directory for external sources; rejected for official sources |
| `blendshape` | External sources: `none` (default) or `matching_template` |

Official appearances use template BlendShapes. External `matching_template` requires a compatible mesh topology. Mesh data is described by `assets.json` in the asset directory.

Top-level `animation_states` and `battle_actions` are rejected. Actions come from source discovery. Parsing validates the descriptor; asset loading and model compatibility are checked during preparation.
