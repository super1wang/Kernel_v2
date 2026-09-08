add_executable(ock_contracts_tests contracts_tests.cpp)
target_link_libraries(ock_contracts_tests PRIVATE OCK::CoreContracts)
target_compile_features(ock_contracts_tests PRIVATE cxx_std_20)
target_compile_options(ock_contracts_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc)
# 固定有序的工厂实现集合；调用者材料、发行工厂和Target夹具均绑定。
set(_contracts_factory_files
  "tests/compile/contracts/test_support.hpp"
  "tests/conformance/core_contracts/factories.hpp"
  "tests/conformance/core_contracts/authority_factories.hpp")
set(_contracts_factory_input "")
set(_contracts_factory_json "[")
foreach(_contracts_relative IN LISTS _contracts_factory_files)
  set(_contracts_file "${PROJECT_SOURCE_DIR}/${_contracts_relative}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_contracts_file}")
  file(SHA256 "${_contracts_file}" _contracts_file_sha)
  string(APPEND _contracts_factory_input "${_contracts_relative}:${_contracts_file_sha}\n")
  if(NOT _contracts_factory_json STREQUAL "[")
    string(APPEND _contracts_factory_json ",")
  endif()
  string(APPEND _contracts_factory_json "{\"path\":\"${_contracts_relative}\",\"sha256\":\"${_contracts_file_sha}\"}")
endforeach()
string(SHA256 _contracts_factory_sha "${_contracts_factory_input}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/factory-inputs.json" "{\"inputs\":${_contracts_factory_json}],\"sha256\":\"${_contracts_factory_sha}\"}\n")
target_compile_definitions(ock_contracts_tests PRIVATE OCK_TEST_FACTORY_SHA="${_contracts_factory_sha}")
get_filename_component(_contracts_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
# 读取实际父 target 的生效选项；每个配置独立身份，夹具不从配置名猜CRT/ASan。
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compile-profile-$<CONFIG>.txt" CONTENT
"compiler=${CMAKE_CXX_COMPILER}
compiler_version=${CMAKE_CXX_COMPILER_VERSION}
cmake_version=${CMAKE_VERSION}
generator=${CMAKE_GENERATOR}
platform=${CMAKE_GENERATOR_PLATFORM}
toolset=${CMAKE_GENERATOR_TOOLSET}
sdk=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}
config=$<CONFIG>
crt=$<TARGET_GENEX_EVAL:ock_contracts_tests,$<TARGET_PROPERTY:ock_contracts_tests,MSVC_RUNTIME_LIBRARY>>
asan=${OCK_ENABLE_ASAN}
options=$<JOIN:$<TARGET_PROPERTY:ock_contracts_tests,COMPILE_OPTIONS>,|>
flags=${CMAKE_CXX_FLAGS}
config_flags=$<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${CMAKE_CXX_FLAGS_RELWITHDEBINFO}>$<$<CONFIG:MinSizeRel>:${CMAKE_CXX_FLAGS_MINSIZEREL}>
" CONDITION "$<COMPILE_LANGUAGE:CXX>" TARGET ock_contracts_tests)
add_custom_command(TARGET ock_contracts_tests POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/discover.py"
    --binary "$<TARGET_FILE:ock_contracts_tests>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/contracts-tests-$<CONFIG>.cmake"
    --runtime-dir "${_contracts_compiler_dir}"
    --includes "$<JOIN:$<TARGET_PROPERTY:ock_contracts_tests,INCLUDE_DIRECTORIES>,|>"
    --generator "${CMAKE_GENERATOR}" --platform "${CMAKE_GENERATOR_PLATFORM}"
    --toolset "${CMAKE_GENERATOR_TOOLSET}" --sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    --config "$<CONFIG>" --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs"
    --profile "${CMAKE_CURRENT_BINARY_DIR}/compile-profile-$<CONFIG>.txt"
  VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/contracts-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "CoreContracts tests require an explicit supported CTest -C configuration")
endif()
set(_contracts_cases "${CMAKE_CURRENT_LIST_DIR}/contracts-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
if(NOT EXISTS "${_contracts_cases}")
  message(FATAL_ERROR "CoreContracts tests have not been built for ${CTEST_CONFIGURATION_TYPE}")
endif()
include("${_contracts_cases}")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/contracts-tests.cmake")
