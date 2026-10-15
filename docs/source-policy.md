# Source resolution

`SourceCatalog` resolves official NPC, Player and Avatar catalog rows. Player identity is the tuple `id/enhanced/skin`.

## NPC and Player creation

NPC selections use their NPC row and private map definition. Player selections use their model prefab and adventure configuration directly through an independent story-actor session.

Player preparation does not require a matching NPC. The selected Avatar ID determines outfit and battle resources; `id/enhanced/skin` keeps Player variants distinct.

## Battle sources

Avatar rows are selected by Avatar ID and deduplicated by configuration/prefab pair. Multiple distinct pairs are ambiguous. Battle preparation errors leave an available exploration source usable.

Authored packages match Player rows by configuration and template-prefab references.

## Display families

`OfficialCharacterFamilies.cpp` groups exact resource references for the menu. Grouping does not combine model, controller or event resources.
