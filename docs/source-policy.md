# Source resolution

`SourceCatalog` resolves official NPC, Player and Avatar catalog rows. Player identity is the tuple `id/enhanced/skin`.

Official skins use `BrowseSkin{skin_id}` and determine their Avatar form. The Player tuple's `skin` selects a Player configuration variant; AvatarSkin IDs belong to `BrowseSkin`.

## NPC and Player creation

NPC selections use their NPC row and private map definition. Player selections create an independent story actor from the selected model prefab and adventure configuration. Their Avatar ID selects outfit and battle resources, and `id/enhanced/skin` identifies the Player variant.

## Battle sources

Avatar rows are selected by Avatar ID and deduplicated by configuration/prefab pair. Multiple distinct pairs are ambiguous. Battle preparation errors leave an available exploration source usable.

Packages with appearance inputs resolve Player rows by configuration and template-prefab references.

## Skin resources

Skin instances retain the base form's action and script identities. The SDK prepares the skin's models, effective event lists, resource mapping and animation sound selection. Each instance uses private event inputs; shared game configurations remain unchanged.

Exploration, battle, skill model changes and default restoration use the same selected skin. An official null mapping suppresses that resource. Layouts retain the SkinID and prepare its resources again when restored.

## Menu organization

Player sources are grouped by official Role ID, with available forms, enhancements and skins as subordinate choices. Each choice retains its own model, controller and event resources. The menu offers combinations present in the source catalog.

The menu displays localized character, path and skin names. Resource references
are available through search and development diagnostics.

## Display families

`OfficialCharacterFamilies` maps selected configuration/model references to
family keys. When an official display name is empty, the menu resolves the
family key through the locale's `source_names` table.
