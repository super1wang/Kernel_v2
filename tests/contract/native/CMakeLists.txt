add_executable(ock_native_tests native_tests.cpp allocation_probe.cpp execution_service_cases.cpp structured_cases.cpp async_cases.cpp session_cases.cpp observation_cases.cpp combined_cases.cpp)
target_link_libraries(ock_native_tests PRIVATE ock_invocation_internal ock_registry_internal ock_policy_internal OCK::CoreContracts)
if(OCK_BUILD_CPU_POOL)
  target_link_libraries(ock_native_tests PRIVATE OCK::Adapter::CpuPool)
  target_compile_definitions(ock_native_tests PRIVATE OCK_NATIVE_EXECUTION_SERVICE_TESTS=1)
endif()
target_include_directories(ock_native_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/compile/contracts")
target_compile_features(ock_native_tests PRIVATE cxx_std_20)
target_compile_options(ock_native_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc /Zc:nrvo- /bigobj)
target_compile_definitions(ock_native_tests PRIVATE "OCK_NATIVE_CONFIGURATION=\"$<CONFIG>\"")
add_subdirectory("${PROJECT_SOURCE_DIR}/examples/native_service" "${PROJECT_BINARY_DIR}/examples/native_service")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-target-$<CONFIG>.json" CONTENT "{\"link_libraries\":\"$<JOIN:$<TARGET_PROPERTY:ock_invocation_internal,LINK_LIBRARIES>,|>\",\"core_contracts_links\":\"$<JOIN:$<TARGET_PROPERTY:OCK::CoreContracts,INTERFACE_LINK_LIBRARIES>,|>\",\"registry_links\":\"$<JOIN:$<TARGET_PROPERTY:ock_registry_internal,LINK_LIBRARIES>,|>\",\"policy_links\":\"$<JOIN:$<TARGET_PROPERTY:ock_policy_internal,LINK_LIBRARIES>,|>\"}\n")
get_filename_component(_native_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_custom_command(TARGET ock_native_tests POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/discover.py"
 --binary "$<TARGET_FILE:ock_native_tests>" --output "${CMAKE_CURRENT_BINARY_DIR}/native-tests-$<CONFIG>.cmake"
 --runtime-dir "${_native_compiler_dir}" --includes "$<JOIN:$<TARGET_PROPERTY:ock_native_tests,INCLUDE_DIRECTORIES>,|>"
 --generator "${CMAKE_GENERATOR}" --platform "${CMAKE_GENERATOR_PLATFORM}" --toolset "${CMAKE_GENERATOR_TOOLSET}"
 --sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" --config "$<CONFIG>" --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs"
 --root-build "${PROJECT_BINARY_DIR}" --target-metadata "${CMAKE_CURRENT_BINARY_DIR}/native-target-$<CONFIG>.json"
 --example "$<TARGET_FILE:ock_native_service>" VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/native-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
 message(FATAL_ERROR "Native tests require explicit CTest -C configuration")
endif()
set(_native_cases "${CMAKE_CURRENT_LIST_DIR}/native-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
if(NOT EXISTS "${_native_cases}")
 message(FATAL_ERROR "Native tests have not been built for this configuration")
endif()
include("${_native_cases}")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/native-tests.cmake")
