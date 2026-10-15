# Contributing

Start with [BUILDING](BUILDING.md) and [modules](docs/modules.md).

- Submit controls through `ApplicationService`; UI and network threads consume copied snapshots, while engine calls run on the official frame.
- Use instance IDs, creation generations and catalog revisions from published values.
- Keep source selection and playback policy in Framework; obtain engine operations through SDK capabilities.
- Put codecs at file/transport boundaries and use typed values in runtime code.
- Update CMake source lists when adding or moving files.
- Test affected interruption, retirement, reentry and persistence paths.
- Keep HTTP code, [OpenAPI](docs/openapi.json) and [protocol documentation](docs/http-api.md) aligned.
- Add UI text through [locale resources](docs/localization.md).
- Run CTest and the product package test.

Keep game assets, local logs, credentials and machine-specific paths out of the repository. Dependency revisions are pinned in [dependencies.json](dependencies.json). Contributions use [WTFPL v2](LICENSE).
