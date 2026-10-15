# Character Framework usage

[简体中文](README.zh-CN.md)

Windows x64, D3D11, Honkai: Star Rail 4.6.0. The package contains `Hksr.Api.dll` and `Hksr.CharacterFramework.dll` and requires the Microsoft Visual C++ x64 runtime.

## Behavior and risks

This is an unofficial third-party mod. It loads `Hksr.Api.dll` and `Hksr.CharacterFramework.dll` into the game process, reads and modifies in-process state, and hooks and calls internal game interfaces to create and control additional characters and their presentation. Layouts, settings and logs are stored in the mod directory.

> [!CAUTION]
> The project is provided as is, without guarantees of safety, stability, compatibility or account safety. Review applicable game and platform rules and back up important data before use. **You decide whether to use this mod and assume the resulting risks and consequences. Do not use it if you cannot accept these risks.**

## Install and launch

1. Download `CharacterFramework-<version>-win64.zip` from [Releases](https://github.com/Charl-Ep/hsr-character-framework/releases/latest). It includes both DLLs.
2. Close the game and extract the complete `CharacterFramework` folder to a permanent, writable location. Preserve the folder structure and keep both DLLs together.
3. Select Star Rail / SRMI in XXMI Launcher. Open **Settings → Advanced → Custom Libraries**, enable **Inject Libraries**, and enter the full DLL paths in this order, one per line:

   ```text
   D:\Mods\CharacterFramework\Hksr.Api.dll
   D:\Mods\CharacterFramework\Hksr.CharacterFramework.dll
   ```

4. Replace the example paths with your own and launch through XXMI Launcher. Confirm these DLLs if prompted for trust. Enter exploration and press **Alt+H** to open the manager.

Register one copy. Update the injection paths if you move the folder. Layouts, settings and logs are stored beside the DLLs. Close the game before updating and preserve `Layouts`, `ui-settings.cfg` and `http-settings.cfg`.

## Runtime behavior

- **Alt+H** toggles the Chinese/English management menu.
- NPC sources use NPC appearance and motions. Player sources switch exploration/battle models with the selected action domain.
- The menu creates/deletes instances and controls actions, placement and gaze.
- Y movement disables ground snapping; Snap restores it. NPC behavior may also control movement.
- Gaze supports automatic mode, character targets and world points. A maximum distance of zero adds no distance limit.
- Each map restores its working layout on return or restart. Actions resume from their beginning. Named snapshots are managed separately.

The features focus on character animation and visual effects. Complex skills can require additional game state. Recognized camera/environment tasks are restricted by default; initial resource loading can stall the frame.

## Integration

[HTTP/SSE](docs/http-api.md) is disabled by default and configured with `http-settings.cfg`. The example file is not read as live settings.

[Application services](docs/application-services.md) describe command results and persistence. [Presentation profiles](docs/presentation-profiles.md) configure phase idle and action completion.

`framework-runtime.log` records startup, commands and errors and is overwritten on launch. Preserve it when diagnosing an issue. To disable the mod, remove both Extra Libraries entries with the game closed.

## License

[WTFPL v2](LICENSE). Dependency notices are in `licenses/`.
