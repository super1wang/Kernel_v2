add_executable(ock_managed_service main.cpp service.cpp)
file(READ "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/execution-list.schema.json" LIST_SCHEMA)
file(READ "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/common.schema.json" COMMON_SCHEMA)
file(READ "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/notifications.schema.json" NOTIFICATIONS_SCHEMA)
configure_file(schemas.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/schemas.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/execution-list.schema.json"
  "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/common.schema.json"
  "${PROJECT_SOURCE_DIR}/schemas/rpc-v1/notifications.schema.json")
target_include_directories(ock_managed_service PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ock_managed_service PRIVATE OCK::Control OCK::Adapter::LocalIPC OCK::Adapter::CpuPool)
