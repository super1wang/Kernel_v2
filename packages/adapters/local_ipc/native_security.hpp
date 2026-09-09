#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ock/local_ipc/pipe.hpp>
namespace ock::local_ipc {
struct NativeSecurity {
  static foundation::Result<std::wstring> endpoint(std::string_view);
  static foundation::Result<PeerIdentity> client(HANDLE);
  static foundation::Result<PeerIdentity> server(HANDLE, std::string_view);
  static foundation::Result<std::string> token_sid(HANDLE);
  static foundation::Result<PSECURITY_DESCRIPTOR> descriptor(const PipeOptions &);
};
}
