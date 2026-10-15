# Source resolution

`SourceCatalog` resolves official NPC, Player and Avatar catalog rows. Player identity is the tuple `id/enhanced/skin`.

## Player carriers

NPC foundation selection uses this order:

1. An explicitly selected NPC ID.
2. Exact resource matches from `OfficialSourcePreferences.cpp`.
3. A unique same-number NPC whose FreeStyle identity matches the Player.
4. A full search for matching FreeStyle identities.

Pending reads remain pending; multiple matches return candidates. Preference rows match Player configuration, NPC configuration and entity-resource paths. Numeric NPC IDs come from the current catalog.

## Protagonist carriers

The preference table maps Boy/Girl paths `00`, `10`, `20`, `30` and `40` to their same-gender base NPC carrier. The selected Avatar ID separately determines outfit and battle resources.

## Battle sources

Avatar rows are selected by Avatar ID and deduplicated by configuration/prefab pair. Multiple distinct pairs are ambiguous. Battle preparation errors leave an available exploration source usable.

Authored packages match Player rows by configuration and template-prefab references.

## Display families

`OfficialCharacterFamilies.cpp` groups exact resource references for the menu. Grouping does not combine model, controller or event resources. Update source preferences and resolver tests together when changing mappings.
