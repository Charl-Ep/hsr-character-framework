# Product distribution is independent of the private integration workspace.
if(NOT HKSR_DEVELOPMENT)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME CharacterFramework)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "CharacterFramework-${PROJECT_VERSION}-win64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/CharacterFramework")
    set(HKSR_DISTRIBUTION_FLAVOR runtime)
else()
    set(HKSR_DISTRIBUTION_FLAVOR development)
endif()
configure_file(cmake/package.json.in "${CMAKE_CURRENT_BINARY_DIR}/package.json.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/distribution/$<CONFIG>/package.json"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/package.json.in")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/distribution/$<CONFIG>/package.json" DESTINATION .)
if(NOT HKSR_DEVELOPMENT)
    include(CPack)
    if(BUILD_TESTING)
        add_test(NAME framework.package COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
            "-DCPACK=${CMAKE_CPACK_COMMAND}" "-DARCHIVE_NAME=CharacterFramework-${PROJECT_VERSION}-win64.zip"
            "-DLOADER=$<TARGET_FILE:CharacterExtensionTests>"
            -P "${CMAKE_CURRENT_LIST_DIR}/TestDistribution.cmake")
        set_tests_properties(framework.package PROPERTIES TIMEOUT 60)
    endif()
endif()
