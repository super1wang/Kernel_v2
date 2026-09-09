#include <ock/local_ipc/pipe.hpp>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
using namespace ock;
int main(int argc,char **argv) {
  if(argc != 3) return 2;
  auto sid = local_ipc::current_user_sid(); if(!sid) return 3;
  local_ipc::PipeOptions options; options.instance = argv[2];
  options.expected_server_sid = *sid; options.allowed_client_sids = {*sid};
  options.io_timeout = std::chrono::seconds(5);
  std::mutex mutex; std::condition_variable changed; bool done = false, success = false;
  std::vector<std::shared_ptr<local_ipc::Connection>> clients;
  const std::string mode = argv[1];
  unsigned requests = 0;
  if(mode == "server" || mode == "first-byte-server" || mode == "deny-server" || mode == "wait-server") {
    if(mode == "deny-server") options.allowed_client_sids = {"S-1-5-18"};
    auto server = local_ipc::Server::listen(options,[&](auto connection) {
      { std::lock_guard lock(mutex); clients.push_back(connection); }
      std::weak_ptr<local_ipc::Connection> weak = connection;
      connection->start([&,weak](data::Payload payload) {
        auto c = weak.lock();
        if(!c || !c->peer() || c->peer()->sid() != *sid) return;
        if(mode == "wait-server") { ++requests; std::cout << "REQUEST" << std::endl; return; }
        auto frame = control::encode_frame(payload);
        if(frame && mode == "first-byte-server") {
          auto ticket = c->reserve(*frame,local_ipc::Queue::Notification);
          if(!ticket || c->start_now(**ticket) != local_ipc::Start::Started) { c->close(); return; }
          if(c->start_now(**ticket) != local_ipc::Start::NotStarted) { c->close(); return; }
        } else if(frame) c->send(*frame);
      },[&] { std::lock_guard lock(mutex); done = true; changed.notify_all(); });
    });
    if(!server) return 4;
    std::cout << "READY" << std::endl;
    { std::unique_lock lock(mutex); changed.wait_for(lock,std::chrono::seconds(15),[&] { return done; }); }
    (*server)->close();
    for(auto &c : clients) if(!c->wait_closed(std::chrono::seconds(5))) return 10;
    return done && (mode != "wait-server" || requests == 1) ? 0 : 5;
  }
  if(mode == "partial-client") {
    auto endpoint = std::string("\\\\.\\pipe\\ock.") + options.instance;
    HANDLE pipe = CreateFileA(endpoint.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
    if(pipe == INVALID_HANDLE_VALUE) return 11;
    auto payload = data::Payload::parse(R"({"text":"中文 partial frame"})");
    auto frame = control::encode_frame(*payload);
    for(auto byte : *frame) { DWORD n = 0; if(!WriteFile(pipe,&byte,1,&n,nullptr) || n != 1) { CloseHandle(pipe); return 12; } }
    std::vector<std::byte> received(frame->size());
    for(auto &byte : received) { DWORD n = 0; if(!ReadFile(pipe,&byte,1,&n,nullptr) || n != 1) { CloseHandle(pipe); return 13; } }
    CloseHandle(pipe);
    return received == *frame ? 0 : 14;
  }
  if(mode == "wrong-server") options.expected_server_sid = "S-1-5-18";
  auto client = local_ipc::connect(options);
  if(mode == "wrong-server") return client ? 9 : 0;
  if(!client) return 6;
  (*client)->start([&](data::Payload value) {
    auto result = value.view().at("text").string();
    std::lock_guard lock(mutex); success = result && *result == "中文 partial frame"; done = true; changed.notify_all();
  },[&] { std::lock_guard lock(mutex); done = true; changed.notify_all(); });
  auto payload = data::Payload::parse(R"({"text":"中文 partial frame"})");
  auto frame = control::encode_frame(*payload);
  if(!frame || !(*client)->send(*frame)) return 7;
  { std::unique_lock lock(mutex); changed.wait_for(lock,std::chrono::seconds(10),[&] { return done; }); }
  (*client)->close();
  // 回调闭包引用本栈；等待连接线程的关闭回调，不能以进程退出掩盖悬空引用。
  if(!(*client)->wait_closed(std::chrono::seconds(5))) return 10;
  return success ? 0 : 8;
}
