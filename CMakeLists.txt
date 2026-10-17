cmake_minimum_required(VERSION 3.25)
project(HksrCharacterFramework VERSION 0.3.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CharacterFramework requires an x64 MSVC-compatible Windows toolchain")
endif()

include(cmake/SdkDependency.cmake)
include(cmake/Dependencies.cmake)
include(CTest)
include(cmake/Sources.cmake)
include(cmake/Localization.cmake)
option(HKSR_DEVELOPMENT "Enable development control and optional observation bridge" OFF)

add_library(framework_minhook STATIC
    "${hksr_minhook_SOURCE_DIR}/src/buffer.c" "${hksr_minhook_SOURCE_DIR}/src/hook.c"
    "${hksr_minhook_SOURCE_DIR}/src/trampoline.c" "${hksr_minhook_SOURCE_DIR}/src/hde/hde64.c")
target_include_directories(framework_minhook PUBLIC "${hksr_minhook_SOURCE_DIR}/include")
add_library(framework_imgui STATIC
    "${hksr_imgui_SOURCE_DIR}/imgui.cpp" "${hksr_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${hksr_imgui_SOURCE_DIR}/imgui_tables.cpp" "${hksr_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${hksr_imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp" "${hksr_imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp")
target_include_directories(framework_imgui PUBLIC "${hksr_imgui_SOURCE_DIR}")

function(framework_target target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    target_include_directories(${target} PUBLIC include)
endfunction()

add_library(CharacterContent STATIC ${HKSR_CONTENT_SOURCES})
framework_target(CharacterContent)
target_link_libraries(CharacterContent PRIVATE framework_json)

add_library(CharacterRuntime STATIC ${HKSR_RUNTIME_SOURCES})
framework_target(CharacterRuntime)
target_link_libraries(CharacterRuntime PRIVATE framework_json)
target_link_libraries(CharacterRuntime PUBLIC Hksr::Api CharacterContent)

add_library(CharacterHttp STATIC src/http/HttpControl.cpp src/http/HttpProtocol.cpp src/http/HttpSettings.cpp)
framework_target(CharacterHttp)
target_link_libraries(CharacterHttp PRIVATE framework_json framework_httplib)
target_compile_definitions(CharacterHttp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(CharacterHttp PUBLIC CharacterRuntime PRIVATE ws2_32 crypt32)

add_library(CharacterUi STATIC ${HKSR_UI_SOURCES} ${LOCALE_GENERATED})
add_dependencies(CharacterUi FrameworkLocales)
target_include_directories(CharacterUi PUBLIC "${LOCALE_OUTPUT}")
framework_target(CharacterUi)
target_compile_definitions(CharacterUi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(CharacterUi PUBLIC HKSR_DEVELOPMENT_UI=$<BOOL:${HKSR_DEVELOPMENT}>)
target_link_libraries(CharacterUi PUBLIC CharacterRuntime framework_imgui PRIVATE framework_minhook
    d3d11 dxgi d3dcompiler dwmapi imm32)

add_library(CharacterFramework SHARED src/host/Extension.cpp)
if(HKSR_DEVELOPMENT)
    target_sources(CharacterFramework PRIVATE tools/DevelopmentControl.cpp tools/VisualProgramInputs.cpp)
    target_link_libraries(CharacterFramework PRIVATE framework_json)
    target_compile_definitions(CharacterFramework PRIVATE HKSR_RESEARCH)
endif()
framework_target(CharacterFramework)
set_target_properties(CharacterFramework PROPERTIES OUTPUT_NAME Hksr.CharacterFramework)
target_link_libraries(CharacterFramework PRIVATE CharacterUi CharacterHttp)
# Refresh even when only the SDK implementation changed and its import library
# did not: POST_BUILD alone leaves an old DLL beside consumer tests.
add_custom_target(FrameworkSdkRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Hksr::Api>" "$<TARGET_FILE_DIR:CharacterFramework>")
add_dependencies(CharacterRuntime FrameworkSdkRuntime)

if(BUILD_TESTING)
    add_executable(CharacterHostIoTests tests/host/HostIoTests.cpp)
    framework_target(CharacterHostIoTests)
    target_link_libraries(CharacterHostIoTests PRIVATE CharacterRuntime)
    add_test(NAME framework.host-io COMMAND CharacterHostIoTests)
    set_tests_properties(framework.host-io PROPERTIES TIMEOUT 10)
    add_test(NAME framework.locale-resources COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/test_localization_generator.py")
    add_test(NAME framework.module-boundaries COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckBoundaries.cmake")
    if(HKSR_DEVELOPMENT)
        add_executable(DevelopmentControlTests tests/development/DevelopmentControlTestMain.cpp
            tests/development/DevelopmentControlTests.cpp tools/DevelopmentControl.cpp
            tests/development/VisualProgramInputsTests.cpp tools/VisualProgramInputs.cpp)
        framework_target(DevelopmentControlTests)
        target_link_libraries(DevelopmentControlTests PRIVATE framework_json)
        target_link_libraries(DevelopmentControlTests PRIVATE CharacterRuntime)
        add_test(NAME framework.development-control COMMAND DevelopmentControlTests)
    endif()
    add_executable(CharacterHttpTests tests/http/HttpTests.cpp)
    framework_target(CharacterHttpTests)
    target_link_libraries(CharacterHttpTests PRIVATE framework_json framework_httplib)
    target_compile_definitions(CharacterHttpTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(CharacterHttpTests PRIVATE CharacterHttp ws2_32 crypt32)
    add_test(NAME framework.http COMMAND CharacterHttpTests)
    set_tests_properties(framework.http PROPERTIES TIMEOUT 30)
    add_executable(CharacterContentTests tests/assets/ContentTests.cpp)
    framework_target(CharacterContentTests)
    target_link_libraries(CharacterContentTests PRIVATE framework_json)
    target_link_libraries(CharacterContentTests PRIVATE CharacterContent)
    add_test(NAME framework.content COMMAND CharacterContentTests)

    add_executable(CharacterRuntimeTests ${HKSR_RUNTIME_TEST_SOURCES})
    target_compile_definitions(CharacterRuntimeTests PRIVATE HKSR_PROFILE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/PresentationProfiles")
    framework_target(CharacterRuntimeTests)
    target_link_libraries(CharacterRuntimeTests PRIVATE framework_json)
    target_link_libraries(CharacterRuntimeTests PRIVATE CharacterRuntime)
    add_test(NAME framework.runtime COMMAND CharacterRuntimeTests)

    add_executable(CharacterPresentTests tests/ui/PresentTests.cpp)
    framework_target(CharacterPresentTests)
    target_link_libraries(CharacterPresentTests PRIVATE CharacterUi)
    add_test(NAME framework.present COMMAND CharacterPresentTests)

    add_executable(CharacterOverlayTests tests/ui/OverlayTests.cpp tests/ui/MenuPreviewTests.cpp
        tests/ui/LocalizationTests.cpp tests/ui/MouseModeTests.cpp)
    framework_target(CharacterOverlayTests)
    target_link_libraries(CharacterOverlayTests PRIVATE CharacterUi windowscodecs ole32)
    add_test(NAME framework.ui COMMAND CharacterOverlayTests)

    add_executable(CharacterExtensionTests tests/host/ExtensionTests.cpp)
    framework_target(CharacterExtensionTests)
    add_dependencies(CharacterExtensionTests CharacterFramework)
    if(HKSR_DEVELOPMENT)
        add_test(NAME framework.dll COMMAND CharacterExtensionTests "$<TARGET_FILE:CharacterFramework>" research)
    else()
        add_test(NAME framework.dll COMMAND CharacterExtensionTests "$<TARGET_FILE:CharacterFramework>" runtime)
    endif()
endif()

install(TARGETS CharacterFramework RUNTIME DESTINATION .)
# Package the runtime copied by this build, not a subsequently replaced SDK
# installation. This is the same DLL exercised by framework.dll.
install(FILES "$<TARGET_FILE_DIR:CharacterFramework>/Hksr.Api.dll" DESTINATION .)
install(FILES LICENSE DESTINATION .)
install(FILES "${hksr_imgui_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses/imgui)
install(FILES "${hksr_minhook_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses/minhook)
install(FILES "${hksr_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION licenses/nlohmann-json)
install(FILES "${hksr_httplib_SOURCE_DIR}/LICENSE" DESTINATION licenses/cpp-httplib)
install(FILES http-settings.example.cfg DESTINATION .)
install(DIRECTORY PresentationProfiles/ DESTINATION PresentationProfiles FILES_MATCHING PATTERN "*.json")
install(FILES docs/http-api.md docs/openapi.json docs/application-services.md docs/source-policy.md docs/presentation-profiles.md docs/visual-programs.md DESTINATION docs)
install(FILES distribution/README.zh-CN.md distribution/README.en.md DESTINATION .)
# The imported SDK supplies its own notices; packaging never reaches into its
# source checkout or assumes that the Framework's dependencies cover the SDK.
install(DIRECTORY "${HksrApi_LICENSE_DIR}/" DESTINATION licenses/HksrApi)
include(cmake/Distribution.cmake)
