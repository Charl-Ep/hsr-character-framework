# Dependencies

[dependencies.json](../dependencies.json) pins the SDK package and upstream commits.

## SDK

`HKSR_SDK_ROOT` selects an extracted installation; `HKSR_SDK_ARCHIVE` selects a ZIP. Set only one. With neither set, CMake uses the manifest's Release download URL. Package registries and cached `HksrApi_DIR` values do not override these inputs.

ROOT accepts compatible newer versions; archives require the manifest version. Both require the specified interface generation. Debug needs a matching Debug package. Clear the previous override when switching inputs.

## Third party

CMake FetchContent downloads sources into the build cache.

| Dependency | Integration | Local source override |
|---|---|---|
| MinHook | Static library | `FETCHCONTENT_SOURCE_DIR_HKSR_MINHOOK` |
| ImGui | Static library | `FETCHCONTENT_SOURCE_DIR_HKSR_IMGUI` |
| nlohmann/json | Header library | `FETCHCONTENT_SOURCE_DIR_HKSR_JSON` |
| cpp-httplib | Header library | `FETCHCONTENT_SOURCE_DIR_HKSR_HTTPLIB` |

Sources have no local patches. Licenses are copied into the product package.

To upgrade, change the manifest and run CTest. cpp-httplib updates also require checking accepted-socket ownership and shutdown integration in `src/http/HttpControl.cpp`, including `serve_guarded` and `process_and_close_socket`.
