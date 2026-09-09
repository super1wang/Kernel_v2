#include "examples/stateless_service/service.hpp"
#include "tests/contract/native/allocation_probe.hpp"
#include <iostream>
#include <new>
namespace allocation=native_test::allocation;
struct Meter final : stateless::Service::MeasurementPort {
  struct Sample { std::string entry; std::uint64_t nanoseconds; allocation::Counts allocations; };
  std::vector<Sample> samples;
  std::string current;
  std::chrono::steady_clock::time_point start;
  Meter() { samples.reserve(40); }
  void begin(std::string_view entry) override {
    current=entry; allocation::start(); start=std::chrono::steady_clock::now();
  }
  void end() override {
    const auto end=std::chrono::steady_clock::now(); auto counts=allocation::stop();
    samples.push_back({current,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()),counts});
  }
};
int main() try {
  allocation::initialize();
  allocation::start(); void *positive=::operator new(65); auto probe=allocation::stop(); ::operator delete(positive);
  if(!probe.cpp || (allocation::crt_available() && !probe.crt) || (allocation::asan_available() && !probe.asan_allocations)) return 2;
  auto sid=ock::local_ipc::current_user_sid(); if(!sid) return 3;
  auto service=stateless::Service::create(*sid); if(!service) return 3;
  Meter meter; auto measured=(*service)->measure(meter); if(!measured || meter.samples.size()!=40) return 4;
  std::cout << "{\"format\":\"ock.b3.entry-costs/1\",\"scope\":\"warm prebound Native versus Dynamic decode plus same HostBound; excludes setup, JSON parsing, wire encoding, IPC and CLI process startup\",\"counter_valid\":true,\"crt_available\":"
      <<(allocation::crt_available()?"true":"false")<<",\"asan_available\":"<<(allocation::asan_available()?"true":"false")<<",\"samples\":[";
  bool comma=false;
  for(auto &sample:meter.samples) {
    if(comma) std::cout<<','; comma=true;
    if(!sample.nanoseconds) return 5;
    std::cout<<"{\"entry\":\""<<sample.entry<<"\",\"ns\":"<<sample.nanoseconds<<",\"cpp_allocations\":"<<sample.allocations.cpp
        <<",\"crt_allocations\":"<<sample.allocations.crt<<",\"asan_allocations\":"<<sample.allocations.asan_allocations
        <<",\"allocated_bytes\":"<<sample.allocations.allocated_bytes<<'}';
  }
  std::cout<<"]}\n"; return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
