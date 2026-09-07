# D1.01 实际代码注册独立于 expected 清单，首次构建后生成 CTest 条目。
add_executable(ock_foundation_tests foundation_tests.cpp)
target_link_libraries(ock_foundation_tests PRIVATE OCK::Foundation)
target_compile_features(ock_foundation_tests PRIVATE cxx_std_20)
target_compile_options(ock_foundation_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
get_filename_component(_foundation_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_custom_command(TARGET ock_foundation_tests POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/discover.py"
    --binary "$<TARGET_FILE:ock_foundation_tests>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/foundation-tests-$<CONFIG>.cmake"
    --runtime-dir "${_foundation_compiler_dir}"
    --includes "$<JOIN:$<TARGET_PROPERTY:ock_foundation_tests,INCLUDE_DIRECTORIES>,|>"
    --generator "${CMAKE_GENERATOR}" --platform "${CMAKE_GENERATOR_PLATFORM}"
    --toolset "${CMAKE_GENERATOR_TOOLSET}" --sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    --config "$<CONFIG>" --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs"
  VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/foundation-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "Foundation tests require an explicit supported CTest -C configuration")
endif()
set(_foundation_cases "${CMAKE_CURRENT_LIST_DIR}/foundation-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
if(NOT EXISTS "${_foundation_cases}")
  message(FATAL_ERROR "Foundation tests have not been built for ${CTEST_CONFIGURATION_TYPE}")
endif()
include("${_foundation_cases}")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/foundation-tests.cmake")
