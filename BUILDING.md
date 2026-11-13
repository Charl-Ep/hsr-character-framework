# Build Character Framework

Requirements: Windows 10+ x64, an MSVC developer terminal, Windows SDK, CMake 3.25+, Ninja and Python 3.9+. Python generates locale tables at build time.

## Configure and build

Framework consumes a precompiled SDK package. Choose one input:

| CMake option | SDK input |
|---|---|
| `HKSR_SDK_ROOT` | Extracted development package; allows compatible newer versions |
| `HKSR_SDK_ARCHIVE` | Development ZIP matching the manifest version |
| Neither | Release URL from `dependencies.json` |

Set only one of ROOT and ARCHIVE. Framework links the package's headers, import library and DLL.

```powershell
cmake --preset release -DHKSR_SDK_ARCHIVE=C:/packages/HksrApi.zip
cmake --build --preset release --parallel 2
ctest --preset release
cmake --install out/release
```

The preset builds in `out/release` and installs to `out/install`. Clear the old override when switching inputs. [Third-party dependencies](docs/dependencies.md) are downloaded on first configuration.

Release requires an SDK using `/MD` and iterator debug level 0; Debug requires `/MDd` and level 2.

## Development build

Configure a separate build directory with `HKSR_DEVELOPMENT=ON` to enable the command mailbox and optional observation bridge. The external `Hksr.RuntimeInspection.dll` can receive model observations when loaded after Framework.

Configure [HTTP/SSE](docs/http-api.md#enable-the-listener) separately; it is disabled by default.

## Tests and packaging

CTest covers codecs, application/runtime behavior, D3D UI, DLL loading and loopback HTTP/SSE. UI font tests use Microsoft YaHei from the Windows fonts directory.

A product build has `HKSR_DEVELOPMENT=OFF`:

```powershell
cpack --config out/release/CPackConfig.cmake -C Release -B out/packages
```

The ZIP contains `CharacterFramework/` with both DLLs, visual scripts, runtime documentation, profiles, configuration example, metadata and licenses. `framework.package` extracts the archive, checks script imports, compiles the packaged script library and loads the packaged DLL.

GitHub Actions builds the Release configuration and uploads this ZIP. Its SDK dependency must be accessible from the runner. Development builds have no CPack target.
