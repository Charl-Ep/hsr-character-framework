find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
file(GLOB LOCALE_INPUTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/locales/*.json")
set(LOCALE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/localization")
set(LOCALE_GENERATED "${LOCALE_OUTPUT}/LocaleIds.hpp" "${LOCALE_OUTPUT}/LocaleData.inc" "${LOCALE_OUTPUT}/LocaleFormats.inc")
add_custom_command(OUTPUT ${LOCALE_GENERATED}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_localization.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/locales" --output "${LOCALE_OUTPUT}"
    DEPENDS ${LOCALE_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_localization.py"
    VERBATIM)
add_custom_target(FrameworkLocales DEPENDS ${LOCALE_GENERATED})
