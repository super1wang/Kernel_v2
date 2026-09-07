# 仅最小上游能力程序，不是产品模块或真实端口 Conformance。
include_guard(GLOBAL)
function(ock_add_dependency_probes)
  if(NOT BUILD_TESTING)
    message(FATAL_ERROR "Dependency probes require BUILD_TESTING=ON")
  endif()
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/dependency-probe-work")
  get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(_probe_tests "")
  foreach(_name IN LISTS OCK_SELECTED_DEPENDENCIES)
    add_executable(ock_probe_${_name} "${OCK_DEPENDENCY_ROOT}/tests/dependencies/probes/${_name}.cpp")
    target_link_libraries(ock_probe_${_name} PRIVATE OCKThirdParty::${_name})
    target_compile_definitions(ock_probe_${_name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ock_probe_${_name} PRIVATE /bigobj)
    if(_name STREQUAL "asio")
      target_link_libraries(ock_probe_asio PRIVATE ws2_32 mswsock)
    endif()
    add_test(NAME T01.dependencies.${_name} COMMAND ock_probe_${_name})
    list(APPEND _probe_tests T01.dependencies.${_name})
  endforeach()
  add_executable(ock_probe_toolchain "${OCK_DEPENDENCY_ROOT}/tests/dependencies/probes/toolchain.cpp")
  add_test(NAME T01.dependencies.toolchain COMMAND ock_probe_toolchain)
  add_test(NAME T01.dependencies.lock_contracts COMMAND "${Python3_EXECUTABLE}" -X utf8 -m unittest discover -s "${OCK_DEPENDENCY_ROOT}/tests/dependencies" -p test_lock.py -v)
  list(APPEND _probe_tests T01.dependencies.toolchain T01.dependencies.lock_contracts)
  if("catch2" IN_LIST OCK_SELECTED_DEPENDENCIES)
    add_test(NAME T01.dependencies.catch2_assertion_guard COMMAND "${Python3_EXECUTABLE}" -X utf8 "${OCK_DEPENDENCY_ROOT}/tools/dependencies/check_probe_failure.py" --mode catch2 -- "$<TARGET_FILE:ock_probe_catch2>" "[.guard-fault]")
    list(APPEND _probe_tests T01.dependencies.catch2_assertion_guard)
  endif()
  if(OCK_ENABLE_ASAN)
    add_executable(ock_probe_asan "${OCK_DEPENDENCY_ROOT}/tests/dependencies/probes/asan.cpp")
    add_test(NAME T23.dependencies.asan_healthy COMMAND ock_probe_asan)
    add_test(NAME T23.dependencies.asan_detects_heap_overflow COMMAND "${Python3_EXECUTABLE}" -X utf8 "${OCK_DEPENDENCY_ROOT}/tools/dependencies/check_probe_failure.py" --mode asan -- "$<TARGET_FILE:ock_probe_asan>" fault)
    list(APPEND _probe_tests T23.dependencies.asan_healthy T23.dependencies.asan_detects_heap_overflow)
  endif()
  set_tests_properties(${_probe_tests} PROPERTIES LABELS "D0.06-a" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/dependency-probe-work" ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_compiler_dir}")
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/toolchain-$<CONFIG>.json" CONTENT "{
  \"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",
  \"compiler_version\": \"${CMAKE_CXX_COMPILER_VERSION}\",
  \"generator\": \"${CMAKE_GENERATOR}\",
  \"toolset\": \"${CMAKE_GENERATOR_TOOLSET}\",
  \"windows_sdk\": \"${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\",
  \"cmake_version\": \"${CMAKE_VERSION}\",
  \"cxx_standard\": 20,
  \"configuration\": \"$<CONFIG>\",
  \"crt\": \"${CMAKE_MSVC_RUNTIME_LIBRARY}\",
  \"asan_requested\": \"${OCK_ENABLE_ASAN}\"
}
")
endfunction()
