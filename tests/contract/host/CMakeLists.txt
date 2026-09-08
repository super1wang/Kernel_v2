add_executable(ock_host_tests host_tests.cpp "${PROJECT_SOURCE_DIR}/tests/contract/native/allocation_probe.cpp")
target_link_libraries(ock_host_tests PRIVATE OCK::Runtime OCK::CoreContracts)
target_include_directories(ock_host_tests PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/compile/contracts")
target_compile_features(ock_host_tests PRIVATE cxx_std_20)
target_compile_options(ock_host_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc /Zc:nrvo-)
target_compile_definitions(ock_host_tests PRIVATE "OCK_NATIVE_CONFIGURATION=\"$<CONFIG>\"")
get_filename_component(_host_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_executable(ock_host_fault_tests fault_main.cpp fault_allocations.cpp fault_host_unit.cpp)
target_link_libraries(ock_host_fault_tests PRIVATE OCK::Runtime OCK::CoreContracts)
target_include_directories(ock_host_fault_tests PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_features(ock_host_fault_tests PRIVATE cxx_std_20)
target_compile_options(ock_host_fault_tests PRIVATE /utf-8 /Zc:__cplusplus /permissive- /EHsc /Zc:nrvo-)
target_link_options(ock_host_fault_tests PRIVATE /VERBOSE:LIB)
foreach(_case T03.host.ready_gate T03.host.start_order
    T22.host.pending_report T22.host.lifecycle_reentrancy T03.host.catalog_registration
    T03.host.native_results T03.host.session_ownership T03.host.delegation_revoke
    T22.host.shutdown_deadline T19.host.logging_ownership
    T22.host.admission_race T22.host.return_lifetime T22.host.shutdown_order
    T23.host.steady_allocation T23.host.owner_reclamation)
  add_test(NAME ${_case} COMMAND ock_host_tests ${_case})
  set_tests_properties(${_case} PROPERTIES LABELS "D1.06" TIMEOUT 90
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_host_runtime_dir}")
endforeach()
foreach(_case T22.host.destructor_guard T23.host.allocation_controls T03.host.configuration T03.host.start_failures T22.host.shutdown_errors)
  add_test(NAME ${_case} COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime.py" --binary "$<TARGET_FILE:ock_host_tests>"
    --case ${_case} --work "${CMAKE_CURRENT_BINARY_DIR}/child-runs" --runtime-dir "${_host_runtime_dir}"
    --fault-binary "$<TARGET_FILE:ock_host_fault_tests>" --configuration "$<CONFIG>")
  set_tests_properties(${_case} PROPERTIES LABELS "D1.06" TIMEOUT 120)
endforeach()
