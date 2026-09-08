add_executable(ock_logging_tests logging_tests.cpp "${PROJECT_SOURCE_DIR}/tests/contract/native/allocation_probe.cpp")
target_link_libraries(ock_logging_tests PRIVATE OCK::Runtime OCK::CoreContracts)
target_compile_features(ock_logging_tests PRIVATE cxx_std_20)
target_compile_options(ock_logging_tests PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive- /Zc:nrvo-)
# 绑定真实实现、合同及资格判定源码；编译标记禁止新描述符套用旧二进制。
set(_logging_sources
  packages/runtime/observability/logging.cpp
  packages/runtime/include/ock/runtime/logging.hpp
  packages/contracts/include/ock/contracts/logging.hpp
  packages/foundation/include/ock/foundation/foundation.hpp
  tests/conformance/logging/CMakeLists.txt
  tests/conformance/logging/logging-tests.cmake.in
  tests/conformance/logging/logging_tests.cpp
  tests/conformance/logging/reference_backend.hpp
  tests/conformance/logging/supplemental_cases.hpp
  tests/conformance/logging/qualification.py
  tests/conformance/logging/verify_contract.py
  tests/conformance/support/identity.py
  tests/contract/native/allocation_probe.cpp
  tests/contract/native/allocation_probe.hpp)
list(SORT _logging_sources)
set(_logging_input "")
set(_logging_json "[")
foreach(_relative IN LISTS _logging_sources)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${_relative}")
  file(SHA256 "${PROJECT_SOURCE_DIR}/${_relative}" _sha)
  string(APPEND _logging_input "${_relative}:${_sha}\n")
  if(NOT _logging_json STREQUAL "[")
    string(APPEND _logging_json ",")
  endif()
  string(APPEND _logging_json "{\"path\":\"${_relative}\",\"sha256\":\"${_sha}\"}")
endforeach()
string(SHA256 _logging_sha "${_logging_input}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/logging-binding.json" "{\"sha256\":\"${_logging_sha}\",\"sources\":${_logging_json}]}\n")
target_compile_definitions(ock_logging_tests PRIVATE OCK_LOGGING_BINDING_SHA="${_logging_sha}")
get_filename_component(_logging_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
configure_file(logging-tests.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/logging-tests.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/logging-tests-$<CONFIG>.cmake"
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/logging-tests.in")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/logging-tests.cmake" [=[
if(NOT CTEST_CONFIGURATION_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR "Logging tests require explicit supported configuration")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/logging-tests-${CTEST_CONFIGURATION_TYPE}.cmake")
]=])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/logging-tests.cmake")
