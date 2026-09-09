#include "backends.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <future>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace executor_test;
void check(bool x){if(!x)throw std::runtime_error("executor contract assertion");}
auto deadline(){return std::chrono::steady_clock::now()+std::chrono::seconds(3);}
auto work(CallbackWork::Function f,CallbackWork::Completion done=[](auto){},std::shared_ptr<WorkReport> r=std::make_shared<WorkReport>()) {
  return std::make_unique<CallbackWork>(std::move(f),std::move(done),std::move(r));
}
int main(int argc,char **argv) try {
  if(argc==2 && std::string_view(argv[1])=="--identity"){std::cout<<OCK_EXECUTOR_MANIFEST_SHA;return 0;}
  check(argc==3);std::string backend=argv[1],test=argv[2];
  if(test=="real_parallel" || test=="stop_timeout") {
    auto pool=create("cpu_pool");std::promise<void> release;auto wait=release.get_future().share();
    struct Threads {
      std::array<HANDLE,2> values{};
      ~Threads(){for(auto value:values)if(value)CloseHandle(value);}
    } threads;
    std::array<DWORD,2> worker_ids{};
    std::atomic<unsigned> entered=0;auto owner=std::make_shared<int>(7);std::weak_ptr<int> weak=owner;
    for(unsigned i=0;i<2;++i)check(bool(pool->submit(work([&,owner,i]{worker_ids[i]=GetCurrentThreadId();++entered;wait.wait();return Result<void>{};}))));
    owner.reset();auto until=deadline();while(entered!=2 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    const bool parallel=entered==2;
    if(parallel)for(unsigned i=0;i<2;++i)threads.values[i]=OpenThread(SYNCHRONIZE,FALSE,worker_ids[i]);
    if(test=="stop_timeout") {
      auto concurrent_drain=std::async(std::launch::async,[&]{return pool->drain_until(deadline());});
      auto stopped=pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(5));
      bool failed=!stopped && stopped.error().code()==executor_error(ExecutorErrc::Timeout).code() && !weak.expired();
      bool rejected=!pool->submit(work([]{return Result<void>{};}));release.set_value();
      check(bool(concurrent_drain.get())&&failed&&rejected);
    } else release.set_value();
    check(bool(pool->shutdown_until(deadline())) && parallel && weak.expired());
    for(auto thread:threads.values)check(thread&&WaitForSingleObject(thread,0)==WAIT_OBJECT_0);
    check(bool(pool->shutdown_until(deadline()))&&bool(pool->drain_until(deadline()))&&!pool->in_worker());
  } else if(test=="inline_reentrancy") {
    auto pool=create("inline");unsigned completed=0;
    check(bool(pool->submit(work([&]{check(bool(pool->submit(work([&]{++completed;return Result<void>{};}))));check(completed==1);++completed;return Result<void>{};}))));
    check(completed==2);
  } else if(test=="controlled_order") {
    auto pool=create("controlled");unsigned n=0;std::vector<unsigned> order;
    for(unsigned i=0;i<3;++i)check(bool(pool->submit(work([&,i]{order.push_back(i);++n;return Result<void>{};}))));
    check(n==0);check(static_cast<TestExecutor&>(*pool).run_one());check(n==1);check(bool(pool->drain_until(deadline())));check(n==3 && order==std::vector<unsigned>({0,1,2}));
  } else {
    auto pool=create(backend);auto report=std::make_shared<WorkReport>();std::atomic<unsigned> done=0;
    auto owner=std::make_shared<int>(8);std::weak_ptr<int> weak=owner;
    if(test=="queue_full") {
      std::promise<void> release;auto ready=release.get_future().share();std::atomic<unsigned> entered=0,accepted=0;std::vector<std::thread> callers;
      auto submit=[&]{if(pool->submit(work([&]{++entered;ready.wait();return Result<void>{};})))++accepted;};
      for(unsigned i=0;i<16;++i){if(backend=="inline")callers.emplace_back(submit);else submit();}
      auto target=backend=="inline"?16u:backend=="cpu_pool"?2u:0u;auto until=deadline();while(entered<target && std::chrono::steady_clock::now()<until)std::this_thread::yield();
      bool filled=entered==target;bool reentered=false;
      auto rejected_owner=std::shared_ptr<int>(new int(3),[&](int*p){delete p;reentered=!pool->submit(work([]{return Result<void>{};}));});std::weak_ptr<int> rejected_weak=rejected_owner;
      auto rejected_work=work([rejected_owner]{return Result<void>{};});rejected_owner.reset();auto rejected=pool->submit(std::move(rejected_work));
      bool clean=!rejected && rejected.error().code()==executor_error(ExecutorErrc::Full).code() && rejected_weak.expired() && reentered;
      release.set_value();for(auto &t:callers)t.join();check(bool(pool->drain_until(deadline())) && accepted==16 && filled && clean);
    } else if(test=="ownership_transfer" || test=="exactly_once" || test=="drain_shutdown") {
      check(bool(pool->submit(work([owner]{return Result<void>{};},[&](auto result){check(bool(result));++done;},report))));
      owner.reset();check(bool(pool->drain_until(deadline())));check(weak.expired() && done==1 && report->executions==1 && report->duplicates==0);
      check(bool(pool->shutdown_until(deadline())));check(bool(pool->shutdown_until(deadline())));
    } else if(test=="rejection_cleanup") {
      check(bool(pool->shutdown_until(deadline())));
      auto request=work([owner]{return Result<void>{};},[&](auto){++done;},report);owner.reset();
      check(!pool->submit(std::move(request)));check(weak.expired() && done==0 && report->executions==0);
      check(bool(pool->drain_until(deadline())));
    } else if(test=="exception_boundary") {
      check(bool(pool->submit(work([]()->Result<void>{throw std::runtime_error("work");},[&](auto result){check(!result);++done;throw std::runtime_error("callback");},report))));
      check(bool(pool->drain_until(deadline())));check(done==1 && report->work_exceptions==1 && report->callback_exceptions==1);
      check(bool(pool->submit(work([]{return Result<void>{};}))));check(bool(pool->drain_until(deadline())));
    } else if(test=="worker_rejection") {
      std::atomic<bool> rejected=false;
      check(bool(pool->submit(work([&]{auto a=pool->drain_until(deadline()),b=pool->shutdown_until(deadline());rejected=pool->in_worker() && !a && !b && a.error().code()==executor_error(ExecutorErrc::WorkerWait).code() && b.error().code()==a.error().code();return Result<void>{};}))));
      check(bool(pool->drain_until(deadline())) && rejected);
      check(bool(pool->submit(work([]{return Result<void>{};}))));check(bool(pool->shutdown_until(deadline())));
    } else throw std::runtime_error("unknown case");
  }
  std::cout<<"ock.executor-conformance/1 "<<backend<<' '<<test<<" passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
