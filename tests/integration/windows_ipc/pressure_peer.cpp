#include <ock/local_ipc/pipe.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
using namespace ock;
void check(bool good) { if(!good) throw std::runtime_error("IPC pressure contract failed"); }
std::vector<std::byte> frame(std::string text) {
  auto p=data::Payload::parse(text); check(bool(p)); auto f=control::encode_frame(*p); check(bool(f)); return std::move(*f);
}
int main(int argc,char **argv) try {
  check(argc==3); const std::string mode=argv[1];
  auto sid=local_ipc::current_user_sid(); check(bool(sid));
  local_ipc::PipeOptions options; options.instance=argv[2]; options.allowed_client_sids={*sid}; options.io_timeout=std::chrono::seconds(3);
  std::mutex mutex; std::condition_variable changed; unsigned closed=0;
  std::vector<std::shared_ptr<local_ipc::Connection>> connections;
  auto server=local_ipc::Server::listen(options,[&](auto connection) {
    { std::lock_guard lock(mutex); connections.push_back(connection); }
    std::weak_ptr<local_ipc::Connection> weak=connection;
    connection->start([&,weak](data::Payload input) {
      auto c=weak.lock(); check(bool(c));
      auto phase=input.view().at("phase").string();
      if(mode=="unknown") {
        if(phase=="fill") {
          const auto exact=frame("{\"padding\":\""+std::string(16384-12-14,'x')+"\"}"); check(exact.size()==16384);
          for(unsigned i=0;i<4;++i) check(bool(c->send(exact)));
          std::cout<<"FILLED_QUEUED"<<std::endl;
        } else {
          auto ticket=c->reserve(frame(R"({"prefix":"authorized"})"),local_ipc::Queue::Control); check(bool(ticket));
          auto start=c->start_now(**ticket);
          std::cout<<(start==local_ipc::Start::Unknown ? "UNKNOWN" : (start==local_ipc::Start::Started ? "STARTED" : "NOT_STARTED"))<<std::endl;
        }
      } else if(mode=="priority") {
        auto notification=frame(R"({"notification":true})");
        for(unsigned i=0;i<10;++i) check(bool(c->send(notification,local_ipc::Queue::Notification)));
        check(bool(c->send(frame(R"({"control":true})"))));
      } else if(phase=="slow") {
        check(bool(c->send(frame("{\"padding\":\""+std::string(1024*1024,'x')+"\"}"))));
        auto notification=frame("{\"padding\":\""+std::string(8192,'y')+"\"}");
        unsigned accepted=0;
        for(unsigned i=0;i<1024;++i) { if(!c->send(notification,local_ipc::Queue::Notification)) break; ++accepted; }
        check(accepted>0 && accepted<1024);
        check(bool(c->send(frame(R"({"control_quota_survived":true})"))));
        std::cout<<"SLOW_QUEUED"<<std::endl;
      } else {
        check(bool(c->send(frame(R"({"fast":true})"))));
      }
    },[&] { std::lock_guard lock(mutex); ++closed; changed.notify_all(); });
  }); check(bool(server));
  std::cout<<"READY"<<std::endl;
  { std::unique_lock lock(mutex); check(changed.wait_for(lock,std::chrono::seconds(12),[&] { return closed >= (mode=="slow" ? 2u : 1u); })); }
  (*server)->close();
  for(auto &c:connections) check(c->wait_closed(std::chrono::seconds(5)));
  std::cout<<"CLOSED"<<std::endl;
  return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
