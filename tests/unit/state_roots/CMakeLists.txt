add_executable(ock_state_roots frozen_roots.cpp)
target_link_libraries(ock_state_roots PRIVATE OCK::CoreContracts)
target_include_directories(ock_state_roots PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/packages/state/include")
add_test(NAME T08.state.frozen_roots COMMAND ock_state_roots)
add_executable(ock_state_snapshots domain_snapshots.cpp)
target_link_libraries(ock_state_snapshots PRIVATE OCK::CoreContracts)
target_include_directories(ock_state_snapshots PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/packages/state/include")
add_test(NAME T08.state.snapshot_ownership COMMAND ock_state_snapshots)
add_executable(ock_state_commit memory_commit.cpp)
target_link_libraries(ock_state_commit PRIVATE OCK::CoreContracts)
target_include_directories(ock_state_commit PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/packages/state/include")
add_test(NAME T09.state.memory_commit COMMAND ock_state_commit)
add_executable(ock_state_atomic atomic_group.cpp)
target_link_libraries(ock_state_atomic PRIVATE OCK::State)
target_include_directories(ock_state_atomic PRIVATE "${PROJECT_SOURCE_DIR}")
add_test(NAME T10.state.atomic_group COMMAND ock_state_atomic)
add_executable(ock_state_runtime runtime_state.cpp)
target_link_libraries(ock_state_runtime PRIVATE OCK::State ock_invocation_internal ock_registry_internal ock_policy_internal)
target_include_directories(ock_state_runtime PRIVATE "${PROJECT_SOURCE_DIR}")
add_test(NAME T06.state.runtime_native COMMAND ock_state_runtime)
if(OCK_BUILD_STATE)
  add_executable(ock_state_objects object_roots.cpp)
  target_link_libraries(ock_state_objects PRIVATE OCK::State)
  target_include_directories(ock_state_objects PRIVATE "${PROJECT_SOURCE_DIR}")
  add_test(NAME T09.state.object_references COMMAND ock_state_objects)
  add_executable(ock_state_cost state_cost.cpp)
  target_link_libraries(ock_state_cost PRIVATE OCK::State)
  target_include_directories(ock_state_cost PRIVATE "${PROJECT_SOURCE_DIR}")
  add_test(NAME T23.state.structural_sharing_cost COMMAND ock_state_cost)

  get_filename_component(_ock_state_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set_tests_properties(
    T08.state.frozen_roots
    T08.state.snapshot_ownership
    T09.state.memory_commit
    T10.state.atomic_group
    T06.state.runtime_native
    T09.state.object_references
    T23.state.structural_sharing_cost
    PROPERTIES ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${_ock_state_runtime_dir}")
endif()
