add_library(ock_registry_internal STATIC "${PROJECT_SOURCE_DIR}/packages/runtime/registry/registry.cpp")
target_link_libraries(ock_registry_internal PRIVATE OCK::CoreContracts)
target_compile_features(ock_registry_internal PRIVATE cxx_std_20)
target_compile_options(ock_registry_internal PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
add_executable(ock_registration_tests registration_tests.cpp)
target_link_libraries(ock_registration_tests PRIVATE ock_registry_internal OCK::CoreContracts)
target_include_directories(ock_registration_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/compile/contracts")
target_compile_features(ock_registration_tests PRIVATE cxx_std_20)
target_compile_options(ock_registration_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
get_filename_component(_registration_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_custom_command(TARGET ock_registration_tests POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/discover.py"
 --binary "$<TARGET_FILE:ock_registration_tests>" --output "${CMAKE_CURRENT_BINARY_DIR}/registration-tests-$<CONFIG>.cmake"
 --runtime-dir "${_registration_compiler_dir}" --includes "$<JOIN:$<TARGET_PROPERTY:ock_registration_tests,INCLUDE_DIRECTORIES>,|>"
 --generator "${CMAKE_GENERATOR}" --platform "${CMAKE_GENERATOR_PLATFORM}" --toolset "${CMAKE_GENERATOR_TOOLSET}"
 --sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" --config "$<CONFIG>" --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs" VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/registration-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
 message(FATAL_ERROR "Registration tests require explicit CTest -C configuration")
endif()
set(_registration_cases "${CMAKE_CURRENT_LIST_DIR}/registration-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
if(NOT EXISTS "${_registration_cases}")
 message(FATAL_ERROR "Registration tests have not been built for this configuration")
endif()
include("${_registration_cases}")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/registration-tests.cmake")
