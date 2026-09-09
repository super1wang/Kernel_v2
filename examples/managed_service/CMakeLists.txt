cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(OCKManagedConsumer LANGUAGES CXX)
  find_package(OCK CONFIG REQUIRED COMPONENTS Control Adapter::LocalIPC Adapter::CpuPool)
  set(_ock_rpc_schema_dir "${OCK_SCHEMA_DIR}/rpc-v1")
else()
  set(_ock_rpc_schema_dir "${PROJECT_SOURCE_DIR}/schemas/rpc-v1")
endif()
add_executable(ock_managed_service main.cpp service.cpp)
file(READ "${_ock_rpc_schema_dir}/execution-list.schema.json" LIST_SCHEMA)
file(READ "${_ock_rpc_schema_dir}/common.schema.json" COMMON_SCHEMA)
file(READ "${_ock_rpc_schema_dir}/notifications.schema.json" NOTIFICATIONS_SCHEMA)
configure_file(schemas.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/schemas.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${_ock_rpc_schema_dir}/execution-list.schema.json"
  "${_ock_rpc_schema_dir}/common.schema.json"
  "${_ock_rpc_schema_dir}/notifications.schema.json")
target_include_directories(ock_managed_service PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ock_managed_service PRIVATE OCK::Control OCK::Adapter::LocalIPC OCK::Adapter::CpuPool)
