#include "probe_common.hpp"
#include <asio.hpp>
#include <asio/windows/stream_handle.hpp>
#include <windows.h>
#include <array>
int main() {
  asio::io_context io;
  const auto name = std::wstring(L"\\\\.\\pipe\\ock-d006-probe-") + std::to_wstring(GetCurrentProcessId());
  HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64, 64, 0, nullptr);
  PROBE_CHECK(server != INVALID_HANDLE_VALUE);
  HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  PROBE_CHECK(client != INVALID_HANDLE_VALUE);
  PROBE_CHECK(ConnectNamedPipe(server, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED);
  asio::windows::stream_handle reader(io, server), writer(io, client);
  std::array<char, 4> output{}; bool read_ok = false, write_ok = false;
  asio::async_read(reader, asio::buffer(output), [&](const asio::error_code& ec, size_t n) { read_ok = !ec && n == 4; });
  asio::async_write(writer, asio::buffer("ab", 2), [&](const asio::error_code& ec, size_t n) {
    if (ec || n != 2) return;
    asio::async_write(writer, asio::buffer("cd", 2), [&](const asio::error_code& ec2, size_t n2) { write_ok = !ec2 && n2 == 2; });
  });
  io.run(); PROBE_CHECK(read_ok && write_ok && std::string(output.data(), 4) == "abcd");
  std::cout << "standalone Asio real overlapped Named Pipe two-write/read smoke\n";
}
