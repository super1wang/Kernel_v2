#include "native_security.hpp"
#include <sddl.h>
#include <algorithm>
namespace ock::local_ipc {
namespace {
template<class T> foundation::Result<T> fail() {
  return foundation::make_unexpected(error(IpcErrc::Authentication));
}
struct Handle {
  HANDLE value = nullptr;
  ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct LocalAllocation {
  HLOCAL value;
  ~LocalAllocation() { if(value) LocalFree(value); }
};
}
foundation::Result<std::wstring> NativeSecurity::endpoint(std::string_view name) {
  if(name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
  })) return foundation::make_unexpected(error(IpcErrc::InvalidEndpoint));
  return std::wstring(L"\\\\.\\pipe\\ock.") + std::wstring(name.begin(), name.end());
}
foundation::Result<std::string> NativeSecurity::token_sid(HANDLE token) {
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 65536) return fail<std::string>();
  std::vector<std::byte> bytes(size);
  if(!GetTokenInformation(token, TokenUser, bytes.data(), size, &size)) return fail<std::string>();
  LPSTR sid = nullptr;
  if(!ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return fail<std::string>();
  LocalAllocation owned{sid};
  std::string result(sid);
  return result;
}
foundation::Result<std::string> current_user_sid() {
  Handle token;
  if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return fail<std::string>();
  return NativeSecurity::token_sid(token.value);
}
foundation::Result<PeerIdentity> NativeSecurity::client(HANDLE pipe) {
  ULONG pid = 0;
  if(!GetNamedPipeClientProcessId(pipe, &pid) || !ImpersonateNamedPipeClient(pipe)) return fail<PeerIdentity>();
  Handle token;
  const bool opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token.value) != FALSE;
  // Revert 失败意味着线程仍带对端身份，不能继续执行任何宿主逻辑。
  if(!RevertToSelf()) std::terminate();
  if(!opened) return fail<PeerIdentity>();
  auto sid = token_sid(token.value);
  if(!sid) return fail<PeerIdentity>();
  return PeerIdentity(std::move(*sid), pid);
}
foundation::Result<PeerIdentity> NativeSecurity::server(HANDLE pipe, std::string_view expected) {
  if(expected.empty()) return fail<PeerIdentity>();
  ULONG pid = 0;
  if(!GetNamedPipeServerProcessId(pipe, &pid)) return fail<PeerIdentity>();
  Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)}, token;
  if(!process.value || !OpenProcessToken(process.value, TOKEN_QUERY, &token.value)) return fail<PeerIdentity>();
  auto sid = token_sid(token.value);
  if(!sid || *sid != expected) return fail<PeerIdentity>();
  return PeerIdentity(std::move(*sid), pid);
}
foundation::Result<PSECURITY_DESCRIPTOR> NativeSecurity::descriptor(const PipeOptions &options) {
  if(options.allowed_client_sids.empty() || options.allowed_client_sids.size() > 128) return fail<PSECURITY_DESCRIPTOR>();
  auto owner = current_user_sid();
  if(!owner) return fail<PSECURITY_DESCRIPTOR>();
  // Protected DACL：宿主可创建实例；客户端只有读/写数据，不授予 FILE_CREATE_PIPE_INSTANCE。
  std::string sddl = "D:P(A;;GA;;;" + *owner + ")";
  for(const auto &sid : options.allowed_client_sids) {
    PSID parsed = nullptr;
    if(!ConvertStringSidToSidA(sid.c_str(), &parsed)) return fail<PSECURITY_DESCRIPTOR>();
    LPSTR canonical = nullptr;
    const bool ok = ConvertSidToStringSidA(parsed, &canonical) != FALSE;
    LocalFree(parsed);
    if(!ok) return fail<PSECURITY_DESCRIPTOR>();
    LocalAllocation owned{canonical};
    std::string normalized(canonical);
    if(normalized != sid) return fail<PSECURITY_DESCRIPTOR>();
    sddl += "(A;;0x0012019b;;;" + sid + ")";
  }
  PSECURITY_DESCRIPTOR result = nullptr;
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1, &result, nullptr))
    return fail<PSECURITY_DESCRIPTOR>();
  return result;
}
}
