# Character Framework

Windows x64 character-management mod using the HksrApi SDK. It manages character instances, actions, placement, gaze and per-map layouts. The runtime uses D3D11 and targets game 4.6.0.

The in-game menu opens with **Alt+H**. [HTTP/SSE](docs/http-api.md) provides external control through the same application service.

## Behavior and risks

This is an unofficial third-party mod. It loads `Hksr.Api.dll` and `Hksr.CharacterFramework.dll` into the game process, reads and modifies in-process state, and hooks and calls internal game interfaces to create and control additional characters and their presentation. Layouts, settings and logs are stored in the mod directory.

> [!CAUTION]
> The project is provided as is, without guarantees of safety, stability, compatibility or account safety. Review applicable game and platform rules and back up important data before use. **You decide whether to use this mod and assume the resulting risks and consequences. Do not use it if you cannot accept these risks.**

## Install and use

Download `CharacterFramework-<version>-win64.zip` from [Releases](https://github.com/Charl-Ep/hsr-character-framework/releases/latest) and extract the complete folder to a permanent, writable location.

In XXMI Launcher, select Star Rail / SRMI and open **Settings → Advanced → Custom Libraries → Inject Libraries**. Enable injection and add the full path to `Hksr.Api.dll`, followed by `Hksr.CharacterFramework.dll`, one per line. Launch the game through XXMI and press **Alt+H** in exploration.

Keep both DLLs and the supplied folders together. Close the game before updating. Full instructions: [简体中文](distribution/README.zh-CN.md) / [English](distribution/README.en.md).

## Development

- [Build and package](BUILDING.md)
- [Modules](docs/modules.md)
- [Application commands and snapshots](docs/application-services.md)
- [Source resolution](docs/source-policy.md)
- [Character packages](FORMAT.md)
- [Presentation profiles](docs/presentation-profiles.md)
- [Localization](docs/localization.md)
- [Contributing](CONTRIBUTING.md)

Framework consumes a precompiled SDK package. Product output contains `Hksr.Api.dll` and `Hksr.CharacterFramework.dll`.

## License

[WTFPL v2](LICENSE). Dependency licenses accompany the package.
