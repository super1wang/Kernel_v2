#include <ock/contracts/executor.hpp>
#include <barrier>
#include <iostream>
#include <thread>
using namespace ock::contracts;
int main() {
  auto report=std::make_shared<WorkReport>();std::atomic<unsigned> completed=0;
  CallbackWork work([]{return Result<void>{};},[&](auto result){if(result)++completed;},report);
  std::barrier start(9);std::vector<std::thread> threads;
  for(unsigned i=0;i<8;++i)threads.emplace_back([&]{start.arrive_and_wait();work.execute();});
  start.arrive_and_wait();for(auto &thread:threads)thread.join();
  if(completed!=1 || report->executions!=1 || report->duplicates!=7)return 1;
  std::cout<<"one completion, seven concurrent duplicates rejected\n";
}
