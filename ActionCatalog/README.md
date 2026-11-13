# Action source data

`sources.json` stores copied SDK configuration values, ability declarations and their original reference paths. The runtime derives action menus from these records.

The loader matches the opaque data revision provided by the SDK and the reader contract. Missing or incompatible records are read from the game. Context-dependent visual configuration remains a runtime query.

Runtime reads and diagnostics are saved to `Cache/ActionCatalog/sources.json` beside the DLL. Maintainers can review and distribute that file here. Unresolved inputs retain their diagnostics; failed reads are retried. Explicit refresh invalidates packaged records, including when their replacement fails.

File parsing and atomic writes run on the framework worker. The game thread consumes typed values through the existing action catalog.
