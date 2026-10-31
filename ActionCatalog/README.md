# Action source data

`sources.json` contains SDK configuration values and ability declarations, including their original reference paths. It contains no game objects, animations or derived menu entries.

The loader matches the official client build, active design-data revision, installed design/hotfix version descriptors and reader contract. Missing or incompatible records are read from the game. Context-dependent visual configuration remains a runtime query.

Reads and diagnostics are saved to `Cache/ActionCatalog/sources.json` beside the DLL. Maintainers can publish that file here after checking its coverage. Nonliteral inputs and unsupported declarations retain their unavailable fields and diagnostics. Read failures are retained for inspection but queried again instead of reused. Explicit refresh invalidations override packaged data, even if the replacement read fails.

File parsing and atomic writes run on the framework worker. The game thread consumes typed values through the existing action catalog.
