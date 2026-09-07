add_library(ock_policy_internal STATIC "${PROJECT_SOURCE_DIR}/packages/runtime/policy/policy.cpp")
target_link_libraries(ock_policy_internal PRIVATE OCK::CoreContracts)
target_compile_features(ock_policy_internal PRIVATE cxx_std_20)
target_compile_options(ock_policy_internal PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
add_executable(ock_policy_tests policy_tests.cpp)
target_link_libraries(ock_policy_tests PRIVATE ock_policy_internal OCK::CoreContracts)
target_include_directories(ock_policy_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/compile/contracts")
target_compile_features(ock_policy_tests PRIVATE cxx_std_20)
target_compile_options(ock_policy_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/policy-target-$<CONFIG>.json" CONTENT "{\"link_libraries\":\"$<JOIN:$<TARGET_PROPERTY:ock_policy_internal,LINK_LIBRARIES>,|>\",\"core_contracts_links\":\"$<JOIN:$<TARGET_PROPERTY:OCK::CoreContracts,INTERFACE_LINK_LIBRARIES>,|>\"}\n")
get_filename_component(_policy_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_custom_command(TARGET ock_policy_tests POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/discover.py"
 --binary "$<TARGET_FILE:ock_policy_tests>" --output "${CMAKE_CURRENT_BINARY_DIR}/policy-tests-$<CONFIG>.cmake"
 --runtime-dir "${_policy_compiler_dir}" --includes "$<JOIN:$<TARGET_PROPERTY:ock_policy_tests,INCLUDE_DIRECTORIES>,|>"
 --generator "${CMAKE_GENERATOR}" --platform "${CMAKE_GENERATOR_PLATFORM}" --toolset "${CMAKE_GENERATOR_TOOLSET}"
 --sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" --config "$<CONFIG>" --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs" --root-build "${PROJECT_BINARY_DIR}" --target-metadata "${CMAKE_CURRENT_BINARY_DIR}/policy-target-$<CONFIG>.json" VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/policy-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
 message(FATAL_ERROR "Policy tests require explicit CTest -C configuration")
endif()
set(_policy_cases "${CMAKE_CURRENT_LIST_DIR}/policy-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
if(NOT EXISTS "${_policy_cases}")
 message(FATAL_ERROR "Policy tests have not been built for this configuration")
endif()
include("${_policy_cases}")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/policy-tests.cmake")
