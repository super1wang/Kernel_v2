#include "tests/conformance/executor/backends.hpp"
#include <future>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <windows.h>
using namespace executor_test;
int main(int argc,char**argv) {
  if(argc!=2)return 81;
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
  auto pool=create(argv[1]);std::promise<void> gate;auto ready=gate.get_future().share();
  if(std::string_view(argv[1])!="cpu_pool")gate.set_value();
  auto submitted=pool->submit(std::make_unique<CallbackWork>([&]()->Result<void>{
    ready.wait();std::cerr<<"worker-destruction-boundary\n"<<std::flush;pool.reset();return {};
  },[](auto){},std::make_shared<WorkReport>()));
  if(!submitted)return 82;
  if(std::string_view(argv[1])=="cpu_pool")gate.set_value();
  else static_cast<TestExecutor&>(*pool).run_one();
  std::this_thread::sleep_for(std::chrono::seconds(5));return 83;
}
