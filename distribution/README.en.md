# Character Framework runtime

[简体中文](README.zh-CN.md)

Windows x64, D3D11, Honkai: Star Rail 4.6.0. The package contains `Hksr.Api.dll` and `Hksr.CharacterFramework.dll` and requires the Microsoft Visual C++ x64 runtime.

## Load

With the game closed, place the package in SRMI's Mods directory. Register these full paths in XXMI Launcher's Extra Libraries, in order:

1. `Hksr.Api.dll`
2. `Hksr.CharacterFramework.dll`

Register one copy. DLL hot-unloading is unsupported; exit the game before replacement. Preserve `Layouts`, `ui-settings.cfg` and `http-settings.cfg` when updating.

## Runtime behavior

- **Alt+H** toggles the Chinese/English management menu.
- NPC sources use NPC appearance and motions. Player sources switch exploration/battle models with the selected action domain.
- The menu creates/deletes instances and controls actions, placement and gaze.
- Y movement disables ground snapping; Snap restores it. NPC behavior may also control movement.
- Gaze supports automatic mode, character targets and world points. A maximum distance of zero adds no distance limit.
- Each map restores its working layout on return or restart. Actions resume from their beginning. Named snapshots are managed separately.

Visual playback does not execute damage or turns. Complex skills can require additional game state. Recognized camera/environment tasks are restricted by default; initial resource loading can stall the frame.

## Integration

[HTTP/SSE](docs/http-api.md) is disabled by default and configured with `http-settings.cfg`. The example file is not read as live settings.

[Application services](docs/application-services.md) describe command results and persistence. [Presentation profiles](docs/presentation-profiles.md) configure phase idle and action completion.

`framework-runtime.log` records startup, commands and errors and is overwritten on launch. Preserve it when diagnosing an issue. To disable the mod, remove both Extra Libraries entries with the game closed.

## License

[WTFPL v2](LICENSE). Dependency notices are in `licenses/`.
