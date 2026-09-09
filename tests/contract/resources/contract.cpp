#include <ock/runtime/resources.hpp>
#include <ock/runtime/scheduler.hpp>
#include "tests/conformance/executor/backends.hpp"
#include "packages/runtime/executions/resource_wait_binding.hpp"
#include <future>
#include <barrier>
#include <iostream>
#include <limits>
#include <thread>
using namespace ock;
using namespace runtime::resources;
void check(bool v){if(!v)throw std::runtime_error("resource assertion");}
auto manager(Options options={}){auto m=ResourceManager::create({{"a",4},{"b",2},{"commit",1,true}},{{"read-a","a"},{"write-a","a"}},options);check(bool(m));return std::move(*m);}
auto acquire(ResourceManager&m,std::initializer_list<Claim> c,Phase p=Phase::Compute,std::function<void(std::uint64_t)> wake={}){return m.acquire(std::span(c.begin(),c.size()),p,std::move(wake));}
int main(int argc,char**argv)try {
  check(argc==2);std::string test=argv[1];auto m=manager();
  if(test=="execution_binding_interleavings") {
    using Binding=runtime::executions::detail::ResourceWaitBinding;
    // 同一真实 ResourceManager；仅控制 acquire 返回与 binding 安装间的先后。
    for(bool cancel_inflight:{false,true}) {
      contracts::PrincipalId principal;principal.bytes[0]=1;
      auto pool=std::make_shared<executor_test::TestExecutor>(false);auto made=runtime::scheduler::Scheduler::create(pool,{{principal}});check(bool(made));
      std::shared_ptr<runtime::scheduler::Scheduler> scheduler=std::move(*made);std::shared_ptr<ResourceManager> resources=manager();
      auto held=acquire(*resources,{{"a",Mode::Exclusive,1}});check(bool(held));
      std::promise<void> acquired,release;auto barrier=acquired.get_future();auto go=release.get_future();
      unsigned calls=0,runs=0,done=0;std::function<void(std::uint64_t)> old_wake;std::uint64_t old_generation=0;
      auto binding=std::make_shared<Binding>(scheduler,resources,std::vector<Claim>{{"a"}},[]{},
        [&](std::span<const Claim> claims,std::function<void(std::uint64_t)> wake) {
          auto result=resources->acquire(claims,Phase::Compute,wake);++calls;
          if(calls==1){check(bool(result)&&bool(result->waiter));old_wake=wake;old_generation=result->waiter->generation();held->lease.reset();wake(old_generation);acquired.set_value();go.wait();}
          return result;
        });
      runtime::scheduler::Request request;request.principal=principal;request.resource_ready=false;request.dependencies_ready=[binding]{binding->dependencies_ready();};
      request.work=[&]{auto lease=binding->start_lease();check(bool(lease));++runs;return Result<void>{};};request.completed=[&](auto result){check(bool(result)!=cancel_inflight);binding->terminal();++done;};
      auto ticket=scheduler->enqueue(std::move(request));check(bool(ticket));binding->publish(*ticket);
      std::thread driver([&]{binding->drive();});barrier.wait();if(cancel_inflight)binding->cancel();release.set_value();driver.join();
      if(!cancel_inflight){check(binding->snapshot().pending&&calls==1);binding->drive();check(calls==2);old_wake(old_generation);binding->drive();check(calls==2);scheduler->pump();pool->run_one();}
      check(done==1&&runs==(cancel_inflight?0u:1u)&&resources->used("a")==0&&resources->snapshot().waiters==0&&scheduler->snapshot().active==0);
    }
  } else if(test=="execution_binding") {
    using Binding=runtime::executions::detail::ResourceWaitBinding;
    contracts::PrincipalId principal;principal.bytes[0]=1;
    auto pool=std::make_shared<executor_test::TestExecutor>(false);auto made=runtime::scheduler::Scheduler::create(pool,{{principal}});check(bool(made));
    std::shared_ptr<runtime::scheduler::Scheduler> scheduler=std::move(*made);std::shared_ptr<ResourceManager> resources=std::move(m);
    unsigned runs=0,done=0,failures=0,signals=0;
    auto enqueue=[&](std::vector<Claim> claims,std::vector<runtime::scheduler::Ticket> dependencies={}) {
      auto binding=std::make_shared<Binding>(scheduler,resources,std::move(claims),[&]{++signals;});
      runtime::scheduler::Request request;request.principal=principal;request.resource_ready=false;request.dependencies=std::move(dependencies);
      request.dependencies_ready=[binding]{binding->dependencies_ready();};
      request.work=[binding,&runs]{auto lease=binding->start_lease();check(bool(lease));++runs;return Result<void>{};};
      request.completed=[binding,&done,&failures](auto result){binding->terminal();++done;if(!result)++failures;};
      auto ticket=scheduler->enqueue(std::move(request));check(bool(ticket));
      check(scheduler->pump()==0);binding->drive();check(!binding->snapshot().accepted);
      binding->publish(*ticket);return std::pair(binding,*ticket);
    };
    auto [parent,p]=enqueue({{"a",Mode::Exclusive,1}});auto [child,c]=enqueue({{"a",Mode::Exclusive,1}},{p});
    child->drive();check(resources->used("a")==0&&!child->snapshot().pending);
    parent->drive();check(resources->used("a")==1);check(scheduler->pump()==1);pool->run_one();check(runs==1&&child->snapshot().pending&&resources->used("a")==0);
    child->drive();check(scheduler->pump()==1);pool->run_one();check(runs==2&&done==2&&resources->used("a")==0);
    auto held=acquire(*resources,{{"a",Mode::Exclusive,1}});check(bool(held));auto [waiting,w]=enqueue({{"a"}});waiting->drive();check(resources->snapshot().waiters==1);
    held->lease.reset();check(waiting->snapshot().pending);waiting->drive();check(resources->used("a")==1);waiting->cancel();check(resources->used("a")==0&&resources->snapshot().waiters==0&&failures==1);
    auto [empty,e]=enqueue({});empty->drive();check(scheduler->pump()==1);pool->run_one();check(runs==3&&resources->snapshot().waiters==0);
    auto [invalid,i]=enqueue({{"missing"}});invalid->drive();check(failures==2&&scheduler->snapshot().active==0);
    auto never=acquire(*resources,{{"a",Mode::Exclusive,1}});check(bool(never));auto [closing,t]=enqueue({{"a"}});closing->drive();check(resources->snapshot().waiters==1);scheduler->close();check(resources->snapshot().waiters==0&&failures==3);never->lease.reset();check(resources->used("a")==0&&signals>0);
  } else if(test=="execution_running_cancel") {
    using Binding=runtime::executions::detail::ResourceWaitBinding;
    contracts::PrincipalId principal;principal.bytes[0]=1;
    auto pool=std::make_shared<executor_test::TestExecutor>(false);auto made=runtime::scheduler::Scheduler::create(pool,{{principal}});check(bool(made));
    std::shared_ptr<runtime::scheduler::Scheduler> scheduler=std::move(*made);std::shared_ptr<ResourceManager> resources=std::move(m);
    auto binding=std::make_shared<Binding>(scheduler,resources,std::vector<Claim>{{"a",Mode::Exclusive,1}},[]{});
    std::promise<void> entered,release;auto started=entered.get_future();auto go=release.get_future();bool done=false;
    runtime::scheduler::Request request;request.principal=principal;request.resource_ready=false;request.dependencies_ready=[binding]{binding->dependencies_ready();};
    request.work=[&]{auto lease=binding->start_lease();check(bool(lease));entered.set_value();go.wait();check(binding->stop_token().stop_requested());return Result<void>{};};
    request.completed=[&](auto result){check(bool(result));binding->terminal();done=true;};
    auto ticket=scheduler->enqueue(std::move(request));check(bool(ticket));binding->publish(*ticket);binding->drive();check(scheduler->pump()==1);
    std::thread worker([&]{pool->run_one();});started.wait();auto cancelled=binding->cancel();check(bool(cancelled)&&*cancelled==runtime::scheduler::Retirement::AlreadyStarted);bool retained=resources->used("a")==1&&scheduler->snapshot().inflight==1;release.set_value();worker.join();check(retained&&done&&resources->used("a")==0&&scheduler->snapshot().worker_delivery==0);
  } else if(test=="alias_read_write_same_slot") {
    auto a=acquire(*m,{{"read-a",Mode::Shared,2}});check(bool(a));check(!acquire(*m,{{"write-a",Mode::Exclusive,1}}));a->lease.reset();check(bool(acquire(*m,{{"write-a",Mode::Exclusive,1}})));
  } else if(test=="multi_claim_partial_failure_zero_occupancy") {
    auto b=acquire(*m,{{"b",Mode::Exclusive,1}});check(bool(b));check(!acquire(*m,{{"a",Mode::Shared,2},{"b",Mode::Shared,1}}));check(m->used("a")==0 && m->used("b")==1);
  } else if(test=="aggregate_overflow") {
    auto big=ResourceManager::create({{"x",std::numeric_limits<std::uint64_t>::max()}},{{"alias","x"}});check(bool(big));auto r=acquire(**big,{{"x",Mode::Shared,std::numeric_limits<std::uint64_t>::max()},{"alias",Mode::Shared,1}});check(!r && r.error().code()==error(Errc::Overflow).code() && (*big)->used("x")==0);
  } else if(test=="exclusive_shared_units") {
    auto a=acquire(*m,{{"a",Mode::Shared,2},{"read-a",Mode::Shared,1}});check(bool(a) && m->used("a")==3);check(!acquire(*m,{{"a",Mode::Shared,2}}));auto b=acquire(*m,{{"a",Mode::Shared,1}});check(bool(b));a->lease.reset();check(m->used("a")==1);b->lease.reset();auto x=acquire(*m,{{"a",Mode::Shared,1},{"write-a",Mode::Exclusive,1}});check(bool(x) && !acquire(*m,{{"a",Mode::Shared,1}}));
  } else if(test=="lease_only_own_release") {
    auto a=acquire(*m,{{"a",Mode::Shared,1}}),b=acquire(*m,{{"a",Mode::Shared,2}});check(bool(a)&&bool(b));auto moved=std::move(a->lease);moved->release();moved->release();check(m->used("a")==2);m.reset();b->lease.reset();
  } else if(test=="lease_reentrant_destruction") {
    for(bool throws:{false,true}) {
      m=manager();auto held=acquire(*m,{{"a",Mode::Exclusive,1}});check(bool(held));
      unsigned wakes=0;std::unique_ptr<ResourceManager::Waiter> first,second;
      auto w1=acquire(*m,{{"a"}},Phase::Compute,[&](auto){
        ++wakes;held->lease.reset();first.reset();second.reset();m.reset();
        if(throws)throw std::runtime_error("reentrant wake failure");
      });check(bool(w1));first=std::move(w1->waiter);
      auto w2=acquire(*m,{{"a"}},Phase::Compute,[&](auto){++wakes;});check(bool(w2));second=std::move(w2->waiter);
      // 删除当前 release 的 handle、manager 和其他 pending waiter 后，仍须安全收尾。
      held->lease->release();check(wakes==1 && !m && !held->lease && !first && !second);
    }
  } else if(test=="waiter_reentrant_destruction") {
    auto held=acquire(*m,{{"a",Mode::Exclusive,1}});check(bool(held));
    std::unique_ptr<ResourceManager::Waiter> waiter;unsigned destroyed=0,wakes=0;
    auto capture=std::shared_ptr<int>(new int(1),[&](int*p){
      ++destroyed;check(waiter->generation()==0);waiter->cancel();waiter.reset();
      m.reset();held->lease.reset();delete p;
    });
    auto w=acquire(*m,{{"a"}},Phase::Compute,[capture,&wakes](auto){++wakes;});check(bool(w));waiter=std::move(w->waiter);capture.reset();
    waiter->cancel();check(destroyed==1 && wakes==0 && !waiter && !m && !held->lease);
  } else if(test=="resource_key_boundaries") {
    auto invalid=[](const auto&r){return !r && r.error().code()==error(Errc::InvalidInput).code();};
    const std::string key(96,'k'),alias(96,'a');
    auto valid=ResourceManager::create({{key,2}},{{alias,key}});check(bool(valid));
    auto a=acquire(**valid,{{alias}});check(bool(a)&&(*valid)->used(key)==1);a->lease.reset();
    for(auto bad:{std::string{},std::string(97,'k'),std::string(1,static_cast<char>(128)),std::string("a\xC3\xA9")}) {
      check(invalid(ResourceManager::create({{bad,1}})));
      check(invalid(ResourceManager::create({{key,1}},{{bad,key}})));
      check(invalid(ResourceManager::create({{key,1}},{{"alias",bad}})));
      check(invalid(acquire(**valid,{{bad}})));
    }
    // A21.1 是 ASCII 字节约束，不能无依据收紧为可打印字符。
    for(unsigned c=0;c<128;++c){std::string ascii(1,static_cast<char>(c));auto one=ResourceManager::create({{ascii,1}});check(bool(one));check(bool(acquire(**one,{{ascii}})));}
    auto unknown=acquire(**valid,{{std::string(96,'u')}});check(!unknown && unknown.error().code()==error(Errc::UnknownKey).code());check((*valid)->snapshot().slots==1 && (*valid)->used(key)==0);
  } else if(test=="unknown_key_bounded") {
    m=manager({1,2});check(!ResourceManager::resource_factory);for(unsigned i=0;i<100;++i)check(!acquire(*m,{{"unknown"+std::to_string(i)}}));check(m->snapshot().slots==3);
    auto held=acquire(*m,{{"a",Mode::Exclusive,1}});auto w=acquire(*m,{{"a"}},Phase::Compute,[](auto){});check(bool(w)&&w->waiter);check(!acquire(*m,{{"a"}},Phase::Compute,[](auto){}));check(!acquire(*m,{{"a"},{"b"},{"read-a"}}));w->waiter.reset();check(m->snapshot().waiters==0);
  } else if(test=="waiter_release_race") {
    for(unsigned round=0;round<100;++round){auto a=acquire(*m,{{"a",Mode::Exclusive,1}}),b=acquire(*m,{{"b",Mode::Exclusive,1}});std::atomic<unsigned> wakes=0;
      auto w=acquire(*m,{{"a"},{"b"}},Phase::Compute,[&](auto){++wakes;});check(bool(w)&&w->waiter&&m->snapshot().index_entries==2);
      std::barrier start(3);std::thread t1([&]{start.arrive_and_wait();a->lease.reset();}),t2([&]{start.arrive_and_wait();b->lease.reset();});start.arrive_and_wait();t1.join();t2.join();check(wakes==1 && m->snapshot().waiters==0 && m->snapshot().index_entries==0);auto retry=acquire(*m,{{"a"},{"b"}});check(bool(retry)&&retry->lease);
    }
  } else if(test=="waiter_cancel") {
    for(unsigned round=0;round<100;++round){auto a=acquire(*m,{{"a",Mode::Exclusive,1}});std::atomic<unsigned> wakes=0;auto w=acquire(*m,{{"a"}},Phase::Compute,[&](auto){++wakes;});std::barrier start(3);std::thread t1([&]{start.arrive_and_wait();a->lease.reset();}),t2([&]{start.arrive_and_wait();w->waiter->cancel();});start.arrive_and_wait();t1.join();t2.join();check(wakes<=1 && m->snapshot().index_entries==0 && m->snapshot().waiters==0);}
    auto a=acquire(*m,{{"a",Mode::Exclusive,1}});unsigned wakes=0;auto w=acquire(*m,{{"a"}},Phase::Compute,[&](auto){++wakes;});w->waiter->cancel();a->lease.reset();check(wakes==0);
  } else if(test=="callback_outside_lock") {
    auto a=acquire(*m,{{"a",Mode::Exclusive,1}}),b=acquire(*m,{{"b",Mode::Exclusive,1}});unsigned wakes=0;std::unique_ptr<ResourceManager::Waiter> replacement;
    auto w=acquire(*m,{{"a"},{"b"}},Phase::Compute,[&](auto g){++wakes;check(m->snapshot().index_entries==0);auto retry=acquire(*m,{{"a"},{"b"}},Phase::Compute,[&](auto){++wakes;});check(bool(retry)&&retry->waiter && retry->waiter->generation()!=g);replacement=std::move(retry->waiter);});
    a->lease.reset();check(wakes==1 && m->used("a")==0 && m->snapshot().waiters==1);b->lease.reset();check(wakes==2 && m->snapshot().waiters==0);
  } else if(test=="compute_commit_separation") {
    check(!acquire(*m,{{"commit"}}));check(bool(acquire(*m,{{"commit"}},Phase::Commit)));check(m->used("commit")==0);
  } else if(test=="parent_wait_rejects_held_child_resources") {
    auto a=acquire(*m,{{"a",Mode::Shared,1}});std::vector<Claim> child={{"write-a",Mode::Exclusive,1}};check(!a->lease->before_child_wait(child));a->lease->release();check(bool(a->lease->before_child_wait(child)) && bool(m->acquire(child,Phase::Compute)));
  } else if(test=="scheduler_resource_wakeup") {
    auto pool=std::make_shared<executor_test::TestExecutor>(true);contracts::PrincipalId p;p.bytes[0]=1;auto made=runtime::scheduler::Scheduler::create(pool,{{p}});check(bool(made));auto &s=**made;
    for(bool expire:{false,true}) {
      auto held=acquire(*m,{{"a",Mode::Exclusive,1}});std::shared_ptr<ResourceManager::Waiter> waiting;std::shared_ptr<ResourceManager::Lease> lease;runtime::scheduler::Ticket ticket=0;unsigned runs=0,done=0;
      runtime::scheduler::Request r;r.principal=p;r.resource_ready=false;if(expire)r.deadline=runtime::scheduler::Time::min();r.work=[&]{++runs;check(bool(lease));return Result<void>{};};r.detach_waiter=[&]{if(waiting)waiting->cancel();};r.completed=[&](auto){++done;lease.reset();};auto enqueued=s.enqueue(std::move(r));check(bool(enqueued));ticket=*enqueued;
      // 底层单控制线程示例先发布稳定 ticket；完整并发 binding 属 B5。
      auto w=acquire(*m,{{"a"}},Phase::Compute,[&](auto){auto retry=acquire(*m,{{"a"}});check(bool(retry));lease=std::move(retry->lease);if(!s.make_ready(ticket))lease.reset();});check(bool(w));waiting=std::move(w->waiter);
      s.pump();held->lease.reset();s.pump();check(done==1 && runs==(expire?0u:1u) && m->snapshot().index_entries==0 && m->used("a")==0);
    }
  } else throw std::runtime_error("unknown resource test");
  std::cout<<test<<" passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
