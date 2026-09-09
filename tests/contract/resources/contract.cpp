#include <ock/runtime/resources.hpp>
#include <ock/runtime/scheduler.hpp>
#include "tests/conformance/executor/backends.hpp"
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
  if(test=="alias_read_write_same_slot") {
    auto a=acquire(*m,{{"read-a",Mode::Shared,2}});check(bool(a));check(!acquire(*m,{{"write-a",Mode::Exclusive,1}}));a->lease.reset();check(bool(acquire(*m,{{"write-a",Mode::Exclusive,1}})));
  } else if(test=="multi_claim_partial_failure_zero_occupancy") {
    auto b=acquire(*m,{{"b",Mode::Exclusive,1}});check(bool(b));check(!acquire(*m,{{"a",Mode::Shared,2},{"b",Mode::Shared,1}}));check(m->used("a")==0 && m->used("b")==1);
  } else if(test=="aggregate_overflow") {
    auto big=ResourceManager::create({{"x",std::numeric_limits<std::uint64_t>::max()}},{{"alias","x"}});check(bool(big));auto r=acquire(**big,{{"x",Mode::Shared,std::numeric_limits<std::uint64_t>::max()},{"alias",Mode::Shared,1}});check(!r && r.error().code()==error(Errc::Overflow).code() && (*big)->used("x")==0);
  } else if(test=="exclusive_shared_units") {
    auto a=acquire(*m,{{"a",Mode::Shared,2},{"read-a",Mode::Shared,1}});check(bool(a) && m->used("a")==3);check(!acquire(*m,{{"a",Mode::Shared,2}}));auto b=acquire(*m,{{"a",Mode::Shared,1}});check(bool(b));a->lease.reset();check(m->used("a")==1);b->lease.reset();auto x=acquire(*m,{{"a",Mode::Shared,1},{"write-a",Mode::Exclusive,1}});check(bool(x) && !acquire(*m,{{"a",Mode::Shared,1}}));
  } else if(test=="lease_only_own_release") {
    auto a=acquire(*m,{{"a",Mode::Shared,1}}),b=acquire(*m,{{"a",Mode::Shared,2}});check(bool(a)&&bool(b));auto moved=std::move(a->lease);moved->release();moved->release();check(m->used("a")==2);m.reset();b->lease.reset();
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
      auto w=acquire(*m,{{"a"}},Phase::Compute,[&](auto){auto retry=acquire(*m,{{"a"}});check(bool(retry));lease=std::move(retry->lease);if(!s.make_ready(ticket))lease.reset();});check(bool(w));waiting=std::move(w->waiter);
      runtime::scheduler::Request r;r.principal=p;r.resource_ready=false;if(expire)r.deadline=runtime::scheduler::Time::min();r.work=[&]{++runs;check(bool(lease));return Result<void>{};};r.detach_waiter=[&]{waiting->cancel();};r.completed=[&](auto){++done;lease.reset();};auto enqueued=s.enqueue(std::move(r));check(bool(enqueued));ticket=*enqueued;s.pump();held->lease.reset();s.pump();check(done==1 && runs==(expire?0u:1u) && m->snapshot().index_entries==0 && m->used("a")==0);
    }
  } else throw std::runtime_error("unknown resource test");
  std::cout<<test<<" passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
