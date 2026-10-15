# Character Framework

Windows x64 character-management mod using the HksrApi SDK. It manages character instances, actions, placement, gaze and per-map layouts. The runtime uses D3D11 and targets game 4.6.0.

The in-game menu opens with **Alt+H**. [HTTP/SSE](docs/http-api.md) provides external control through the same application service.

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

Runtime setup: [English](distribution/README.en.md) / [简体中文](distribution/README.zh-CN.md).

## License

[WTFPL v2](LICENSE). Dependency licenses accompany the package.
