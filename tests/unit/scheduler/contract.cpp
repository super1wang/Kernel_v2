#include <ock/runtime/scheduler.hpp>
#include "tests/conformance/executor/backends.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>
#include <barrier>
using namespace ock;
using namespace runtime::scheduler;
void check(bool v){if(!v)throw std::runtime_error("scheduler assertion");}
contracts::PrincipalId principal(unsigned n){contracts::PrincipalId p;p.bytes[0]=static_cast<std::uint8_t>(n);return p;}
Request request(unsigned p=1){Request r;r.principal=principal(p);r.work=[]{return Result<void>{};};r.completed=[](auto){};return r;}
struct Fault final : contracts::ExecutorPort {
  std::string mode;std::unique_ptr<contracts::ReadyWork> held;
  Result<void> submit(std::unique_ptr<contracts::ReadyWork> w) override {
    if(mode=="inline_reject" || mode=="inline_throw"){w->execute();if(mode=="inline_throw")throw 7;return foundation::make_unexpected(error(Errc::ExecutorRejected));}
    if(mode=="duplicate"){w->execute();w->execute();return {};}
    held=std::move(w);if(mode=="throw")throw 8;
    return mode=="reject"?Result<void>(foundation::make_unexpected(error(Errc::ExecutorRejected))):Result<void>{};
  }
};
int main(int argc,char**argv)try {
  check(argc==2);std::string test=argv[1];
  if(test=="fault_attempts") {
    for(auto mode:{"reject","throw","inline_reject","inline_throw","duplicate"}) {
      auto f=std::make_shared<Fault>();f->mode=mode;auto made=Scheduler::create(f,{{principal(1)}});check(bool(made));auto &s=**made;
      unsigned runs=0,done=0;bool success=false;auto r=request();r.work=[&]{++runs;return Result<void>{};};r.completed=[&](auto v){++done;success=bool(v);};
      check(bool(s.enqueue(std::move(r))));check(s.pump()==1);if(f->held){check(s.snapshot().worker_delivery==1 && s.snapshot().inflight==0);f->held->execute();f->held.reset();}
      bool inline_done=std::string(mode).starts_with("inline") || std::string(mode)=="duplicate";
      check(done==1 && runs==(inline_done?1u:0u) && success==inline_done && s.snapshot().worker_delivery==0 && s.snapshot().inflight==0 && s.snapshot().executor_violations>=1);
    }
  } else if(test=="delivery_deadline") {
    auto f=std::make_shared<Fault>();f->mode="hold";Options o;o.worker_delivery=1;auto made=Scheduler::create(f,{{principal(1)}},o);check(bool(made));auto &s=**made;
    unsigned runs=0,done=0,detached=0;auto r=request();auto expires=std::chrono::steady_clock::now()+std::chrono::hours(1);r.deadline=expires;r.work=[&]{++runs;return Result<void>{};};r.detach_waiter=[&]{++detached;};r.completed=[&](auto x){if(!x)++done;};
    check(bool(s.enqueue(std::move(r))));check(s.pump()==1 && s.next_deadline()==expires);s.pump(expires);
    check(done==1 && detached==1 && s.snapshot().inflight==0 && s.snapshot().worker_delivery==1);
    check(bool(s.enqueue(request())));check(s.pump()==0);f->held->execute();f->held.reset();check(runs==0 && s.snapshot().worker_delivery==0);check(s.pump()==1);f->held->execute();f->held.reset();
  } else if(test=="start_deadline_race") {
    unsigned started=0,expired=0;
    for(unsigned round=0;round<100;++round) {
      auto f=std::make_shared<Fault>();f->mode="hold";auto made=Scheduler::create(f,{{principal(1)}});check(bool(made));auto &s=**made;
      std::atomic<unsigned> runs=0,done=0;std::atomic<bool> success=false;auto r=request();auto expires=std::chrono::steady_clock::now()+std::chrono::hours(1);r.deadline=expires;
      r.work=[&]{++runs;return Result<void>{};};r.completed=[&](auto v){success=bool(v);++done;};check(bool(s.enqueue(std::move(r))));check(s.pump()==1);
      std::barrier start(3);std::thread a([&]{start.arrive_and_wait();f->held->execute();}),b([&]{start.arrive_and_wait();s.pump(expires);});start.arrive_and_wait();a.join();b.join();
      check(done==1 && runs==(success?1u:0u) && s.snapshot().inflight==0 && s.snapshot().worker_delivery==1);if(success)++started;else ++expired;
      f->held.reset();check(s.snapshot().worker_delivery==0);
    }
    std::cout<<"start_claim_won="<<started<<" expiration_won="<<expired<<" exactly_one_completion=100\n";
  } else if(test=="dependency_attach_race") {
    auto pool=std::make_shared<executor_test::TestExecutor>(false);Options o;o.history=1000;auto made=Scheduler::create(pool,{{principal(1)}},o);check(bool(made));auto &owner=**made;
    unsigned done=0;
    for(unsigned round=0;round<100;++round) {
      auto p=owner.enqueue(request());check(bool(p));check(owner.pump()==1);
      auto r=request();r.dependencies={*p};r.completed=[&](auto v){if(v)++done;};Result<Ticket> child=foundation::make_unexpected(error(Errc::UnknownTicket));
      std::barrier start(3);std::thread a([&]{start.arrive_and_wait();pool->run_one();}),b([&]{start.arrive_and_wait();child=owner.enqueue(std::move(r));});start.arrive_and_wait();a.join();b.join();
      check(bool(child));check(owner.pump()==1);pool->run_one();check(done==round+1 && owner.snapshot().dependency_edges==0 && owner.snapshot().active==0);
    }
  } else if(test=="executor_owner_lifetime") {
    auto pool=std::make_shared<executor_test::TestExecutor>(false);auto made=Scheduler::create(pool,{{principal(1)}});check(bool(made));unsigned done=0;
    auto r=request();r.completed=[&](auto){++done;};check(bool((*made)->enqueue(std::move(r))));(*made)->pump();made->reset();
    check(bool(pool->drain_until(Time::max())) && done==1);
    auto expired=Scheduler::create(pool,{{principal(1)}});check(bool(expired));pool.reset();auto q=request();q.completed=[&](auto v){if(!v)++done;};check(bool((*expired)->enqueue(std::move(q))));(*expired)->pump();check(done==2 && (*expired)->snapshot().worker_delivery==0);
  } else if(test=="started_deadline") {
    auto created=cpu_pool::Executor::create();check(bool(created));std::shared_ptr<contracts::ExecutorControlPort> pool=std::move(*created);
    auto made=Scheduler::create(pool,{{principal(1)}});check(bool(made));std::promise<void> entered,release;auto ready=entered.get_future();auto go=release.get_future().share();std::atomic<unsigned> done=0;
    auto r=request();auto expires=std::chrono::steady_clock::now()+std::chrono::hours(1);r.deadline=expires;r.work=[&]{entered.set_value();go.wait();return Result<void>{};};r.completed=[&](auto v){if(v)++done;};
    check(bool((*made)->enqueue(std::move(r))));(*made)->pump();bool started=ready.wait_for(std::chrono::seconds(3))==std::future_status::ready;
    if(started)(*made)->pump(expires);bool unchanged=done==0 && (*made)->snapshot().inflight==1 && !(*made)->next_deadline();release.set_value();check(bool(pool->drain_until(Time::max())) && started && unchanged && done==1);
  } else if(test=="completion_backpressure") {
    auto created=cpu_pool::Executor::create({4,16});check(bool(created));std::shared_ptr<contracts::ExecutorControlPort> pool=std::move(*created);Options o;o.global_queued=2;o.subject_queued=2;o.max_inflight=2;o.worker_delivery=2;
    auto made=Scheduler::create(pool,{{principal(1),1,2}},o);check(bool(made));auto &s=**made;std::promise<void> entered,release;auto ready=entered.get_future();auto go=release.get_future().share();std::atomic<unsigned> done=0;
    auto first=request();first.completed=[&](auto){entered.set_value();go.wait();++done;};check(bool(s.enqueue(std::move(first))));s.pump();bool started=ready.wait_for(std::chrono::seconds(3))==std::future_status::ready;unsigned accepted=0;
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    if(started)for(unsigned i=0;i<10;++i){auto r=request();r.completed=[&](auto v){if(v)++done;};if(!s.enqueue(std::move(r)))break;++accepted;s.pump();while(s.snapshot().inflight && std::chrono::steady_clock::now()<until)std::this_thread::yield();}
    bool bounded=s.snapshot().active<=4;release.set_value();check(bool(pool->drain_until(Time::max())) && started && bounded && accepted==4 && done==5 && s.snapshot().active==0);
  } else if(test=="real_concurrent_completion") {
    auto created=cpu_pool::Executor::create({4,16});check(bool(created));std::shared_ptr<contracts::ExecutorControlPort> pool=std::move(*created);Options o;o.max_inflight=16;o.worker_delivery=16;
    auto made=Scheduler::create(pool,{{principal(1),1,8},{principal(2),1,8}},o);check(bool(made));std::atomic<unsigned> done=0;
    for(unsigned i=0;i<200;++i){auto r=request(i%2+1);r.completed=[&](auto v){if(v)++done;};check(bool((*made)->enqueue(std::move(r))));}
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(done<200 && std::chrono::steady_clock::now()<until){(*made)->pump();std::this_thread::yield();}
    check(bool(pool->drain_until(Time::max())));check(done==200 && (*made)->snapshot().active==0 && (*made)->snapshot().inflight==0 && (*made)->snapshot().worker_delivery==0);
  } else {
    auto pool=std::make_shared<executor_test::TestExecutor>(test=="inline_reentry" || test=="history_scale");
    Options o;o.max_inflight=8;o.worker_delivery=8;
    if(test=="quota_control"){o.global_queued=3;o.subject_queued=2;o.max_inflight=2;o.worker_delivery=2;o.control_slots=2;}
    auto made=Scheduler::create(pool,{{principal(1),1,1},{principal(2),3,1}},o);check(bool(made));auto &s=**made;
    if(test=="inline_reentry") {
      unsigned n=0;auto r=request();r.completed=[&](auto v){if(v){++n;check(bool(s.enqueue(request(2))));check(s.pump()==0);check(s.snapshot().inflight==0);}};
      check(bool(s.enqueue(std::move(r))));check(s.pump()==2 && n==1 && s.snapshot().active==0 && s.snapshot().worker_delivery==0);
    } else if(test=="quota_control") {
      check(bool(s.enqueue(request())) && bool(s.enqueue(request())) && !s.enqueue(request()));check(bool(s.enqueue(request(2))) && !s.enqueue(request(2)));
      unsigned control=0;check(bool(s.post_control([&]{++control;check(s.snapshot().queued==3);})));check(bool(s.post_control([&]{++control;})));check(!s.post_control([]{}));
      check(s.pump()==2 && control==2);auto snap=s.snapshot();check(snap.queued==1 && snap.inflight==2 && snap.worker_delivery==2);check(s.pump()==0);pool->drain_until(Time::max());s.pump();pool->drain_until(Time::max());check(s.snapshot().active==0);
    } else if(test=="dependencies_deadline") {
      auto p=request();p.resource_ready=false;auto parent=s.enqueue(std::move(p));check(bool(parent));unsigned expired=0;
      for(unsigned i=0;i<200;++i){auto r=request(2);r.dependencies={*parent};r.deadline=Time::min();r.completed=[&](auto x){if(!x)++expired;};check(bool(s.enqueue(std::move(r))));s.pump();check(s.snapshot().dependency_edges==0);}
      check(expired==200 && s.snapshot().queued==1);auto child=request(2);child.dependencies={*parent};check(bool(s.enqueue(std::move(child))));check(s.pump()==0);check(bool(s.make_ready(*parent)));check(s.pump()==1);pool->run_one();check(s.pump()==1);pool->run_one();check(s.snapshot().active==0);
    } else if(test=="dependency_invalid") {
      auto self=request();self.dependencies={1};check(!s.enqueue(std::move(self)));auto unknown=request();unknown.dependencies={99};check(!s.enqueue(std::move(unknown)));
      auto p=s.enqueue(request());check(bool(p));auto dup=request();dup.dependencies={*p,*p};check(!s.enqueue(std::move(dup)));s.pump();pool->run_one();
      auto completed=request();completed.dependencies={*p};check(bool(s.enqueue(std::move(completed))));check(s.snapshot().dependency_edges==0 && s.pump()==1);pool->run_one();check(s.snapshot().active==0);
    } else if(test=="dependency_failure") {
      auto r=request();r.work=[]{return Result<void>(foundation::make_unexpected(error(Errc::WorkException)));};auto a=s.enqueue(std::move(r));check(bool(a));unsigned done=0;auto c=request(2);c.dependencies={*a};c.completed=[&](auto v){if(!v)++done;};check(bool(s.enqueue(std::move(c))));s.pump();pool->drain_until(Time::max());check(done==1 && s.snapshot().active==0 && s.snapshot().dependency_edges==0);
    } else if(test=="fairness") {
      // 始终保持两个主体 Runnable；主体并发上限不参与本次单步权重测量。
      std::vector<unsigned> order;for(unsigned p=1;p<=2;++p)for(unsigned i=0;i<120;++i){auto r=request(p);r.priority=p==1?0:7;r.work=[&,p]{order.push_back(p);return Result<void>{};};check(bool(s.enqueue(std::move(r))));}
      for(unsigned i=0;i<120;++i){check(s.pump(std::chrono::steady_clock::now(),1)==1);pool->run_one();}
      unsigned a=0,b=0,max_gap=0,gap=0;for(auto p:order){if(p==1){++a;gap=0;}else{++b;max_gap=std::max(max_gap,++gap);}}
      std::cout<<"dispatch_order=";for(auto p:order)std::cout<<p;std::cout<<'\n';
      std::cout<<"fairness principal1="<<a<<" principal2="<<b<<" max_gap="<<max_gap<<'\n';check(a==30 && b==90 && max_gap==3);
      s.close();
    } else if(test=="priority_bound") {
      std::vector<unsigned> order;for(unsigned p:{0,7})for(unsigned i=0;i<100;++i){auto r=request();r.priority=p;r.work=[&,p]{order.push_back(p);return Result<void>{};};check(bool(s.enqueue(std::move(r))));}
      for(unsigned i=0;i<90;++i){s.pump(std::chrono::steady_clock::now(),1);pool->run_one();}
      unsigned low=0,gap=0,max_gap=0;for(auto p:order){if(!p){++low;gap=0;}else max_gap=std::max(max_gap,++gap);}std::cout<<"priority0_dispatches="<<low<<" max_priority7_gap="<<max_gap<<'\n';check(low==10 && max_gap<=8);s.close();
    } else if(test=="history_scale") {
      for(unsigned scale:{0,100,1000,10000}) {
        while(s.snapshot().history<scale){check(bool(s.enqueue(request())));s.pump();}
        auto before=s.snapshot();auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<1000;++i)s.pump();auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        auto inspections=s.snapshot().subject_inspections-before.subject_inspections;std::cout<<"history="<<scale<<" pump_count=1000 inspections="<<inspections<<" nanoseconds="<<duration<<'\n';check(inspections==2000);
      }
    } else if(test=="close_and_callbacks") {
      unsigned done=0;auto owner=std::make_shared<int>(3);std::weak_ptr<int> weak=owner;auto r=request();r.resource_ready=false;r.completed=[&,owner](auto){++done;check(s.snapshot().inflight==0);throw 1;};check(bool(s.enqueue(std::move(r))));check(bool(s.post_control([owner]{})));owner.reset();s.close();check(done==1 && weak.expired() && s.snapshot().controls==0 && s.snapshot().callback_errors==1 && !s.enqueue(request()));
    } else throw std::runtime_error("unknown scheduler test");
  }
  std::cout<<test<<" passed\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
