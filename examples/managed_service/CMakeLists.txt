add_executable(ock_managed_service main.cpp service.cpp)
target_link_libraries(ock_managed_service PRIVATE OCK::Control OCK::Adapter::LocalIPC OCK::Adapter::CpuPool)
