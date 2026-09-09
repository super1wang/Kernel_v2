#include "fixtures.hpp"
#include "allocation_probe.hpp"
#include "packages/runtime/executions/execution_observation.hpp"
#include <future>
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#endif
namespace native_test {
namespace {
using Table=executions::detail::ExecutionTable;
using Source=executions::detail::ExecutionObservation;
PhaseConditions conditions() {return {{RequiredRecordState::NotRequired,0,0},{},{},false,false};}
std::shared_ptr<Table::Entry> entry(const std::shared_ptr<Table>& table,unsigned id) {
  ExecutionRef ref;ref.execution_id.bytes[0]=static_cast<unsigned char>(id);
  SummaryInput summary{ref,key(),policy_test::principal(),{},ExecutionPhase::Queued,table->host(),*ObservationVersion::create(1),{}, {}};
  auto value=table->prepare(summary,std::make_shared<int>(1),CppTypeToken::of<int>(),4,4);CHECK(value);return *value;
}
struct Receiver:ObservationReceiver {
  std::vector<ChangeHint> hints;
  void changed(ChangeHint hint)noexcept override {hints.push_back(std::move(hint));}
};
struct Barrier {
  std::promise<void> entered,release;
  std::shared_future<void> released=release.get_future().share();
  std::atomic<bool> reentrant=false,destroyed=false;
};
struct BlockingReceiver:ObservationReceiver {
  std::shared_ptr<Barrier> barrier;
  std::function<bool()> check;
  BlockingReceiver(std::shared_ptr<Barrier> value,std::function<bool()> verify):barrier(std::move(value)),check(std::move(verify)) {}
  ~BlockingReceiver()override {barrier->destroyed=true;}
  void changed(ChangeHint)noexcept override {
    barrier->reentrant=check();barrier->entered.set_value();barrier->released.wait();
  }
};
struct Release {std::shared_ptr<Barrier> barrier;~Release(){try{barrier->release.set_value();}catch(...) {}}};
}
void observation_journal() {
  policy_test::Env auth;HostIncarnation id;id.bytes[0]=93;
  Table::Limits limits;limits.notice_entries=2;limits.terminal_records=1;
  auto made=Table::create(limits,id);CHECK(made);auto table=*made;
  auto source=Source::create(table,2);CHECK(source);
  auto value=entry(table,1);std::weak_ptr<const void> payload=value->payload();
  std::uint64_t cursor=0;CHECK(!table->next_notice(cursor).notice);
  auto receiver=std::make_shared<Receiver>();
  auto lease=(*source)->observe_changes(auth.caller->view(),{{value->execution()},{},{ObservationTopic::Phase}},receiver);CHECK(lease);
  CHECK((*source)->pump(1)&&receiver->hints.empty());
  allocation::initialize();allocation::start();
  auto published=table->publish(value);
  auto finalizing=table->transition(value,ExecutionPhase::Finalizing,conditions());
  auto terminal=table->transition(value,ExecutionPhase::Terminal,conditions());
  auto counts=allocation::stop();CHECK(published&&finalizing&&terminal);
  CHECK(counts.cpp==0&&counts.asan_allocations==0);
  auto oldest=table->next_notice(cursor);CHECK(oldest.notice&&oldest.gap&&oldest.notice->version==2);
  auto newest=table->next_notice(cursor);CHECK(newest.notice&&!newest.gap&&newest.notice->version==3);
  CHECK(!table->next_notice(cursor).notice);
  CHECK((*source)->pump(2));CHECK(receiver->hints.size()==2&&receiver->hints[0].gap);
  for(auto& hint:receiver->hints)CHECK(hint.summary->value().phase==ExecutionPhase::Terminal&&hint.summary->value().version.value()==3);
  auto late_receiver=std::make_shared<Receiver>();
  auto late=(*source)->observe_changes(auth.caller->view(),{{value->execution()},{},{ObservationTopic::Phase}},late_receiver);CHECK(late);
  CHECK((*source)->pump(4)&&late_receiver->hints.empty()); // 不补发已终结历史。
  auto second=entry(table,2);CHECK(table->publish(second));
  CHECK(table->transition(second,ExecutionPhase::Finalizing,conditions()));CHECK(table->transition(second,ExecutionPhase::Terminal,conditions()));
  value.reset();CHECK(table->trim(16)==1&&payload.expired()); // lease/通知摘要没有结果 pin。
  lease->reset();late->reset();CHECK((*source)->used()==0);
  auto hidden=entry(table,3);auto before=table->notice_cursor();
  CHECK(table->transition(hidden,ExecutionPhase::Finalizing,conditions()));CHECK(table->transition(hidden,ExecutionPhase::Terminal,conditions()));
  CHECK(table->notice_cursor()==before);CHECK(table->publish(hidden));
  auto accepted=table->next_notice(before);CHECK(accepted.notice&&accepted.notice->version==3);
  auto draining_receiver=std::make_shared<Receiver>();
  auto draining=(*source)->observe_changes(auth.caller->view(),{{},policy_test::principal(),{ObservationTopic::Phase}},draining_receiver);CHECK(draining);
  (*source)->stop_accepting();
  CHECK(!(*source)->observe_changes(auth.caller->view(),{{},policy_test::principal(),{ObservationTopic::Phase}},std::make_shared<Receiver>()));
  auto during_drain=entry(table,4);CHECK(table->publish(during_drain));
  CHECK((*source)->pump(1)&&draining_receiver->hints.size()==1);
  auto finished=(*source)->finish_until(std::chrono::steady_clock::now());CHECK(finished&&*finished);
}
void observation_lease_drain() {
  policy_test::Env auth;HostIncarnation id;id.bytes[0]=94;
  auto made=Table::create({},id);CHECK(made);auto table=*made;
  auto source=Source::create(table,1);CHECK(source);auto value=entry(table,1);CHECK(table->publish(value));
  auto barrier=std::make_shared<Barrier>();auto entered=barrier->entered.get_future();
  auto receiver=std::make_shared<BlockingReceiver>(barrier,[source=*source]{return !source->finish_until(std::chrono::steady_clock::now())&&!source->pump(1);});
  auto lease=(*source)->observe_changes(auth.caller->view(),{{value->execution()},{},{ObservationTopic::Phase}},receiver);CHECK(lease);receiver.reset();
  CHECK(table->transition(value,ExecutionPhase::Finalizing,conditions()));
  auto pumping=std::async(std::launch::async,[source=*source]{return source->pump(1);});Release release{barrier};
  CHECK(entered.wait_for(std::chrono::seconds(2))==std::future_status::ready&&barrier->reentrant);
  lease->reset();CHECK((*source)->used()==1&&!barrier->destroyed);
  CHECK(!(*source)->observe_changes(auth.caller->view(),{{value->execution()},{},{ObservationTopic::Phase}},std::make_shared<Receiver>()));
  auto waiting=(*source)->finish_until(std::chrono::steady_clock::now());CHECK(waiting&&!*waiting);
  barrier->release.set_value();CHECK(pumping.get());CHECK(barrier->destroyed&&(*source)->used()==0);
  auto done=(*source)->finish_until(std::chrono::steady_clock::now());CHECK(done&&*done);
}
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
namespace {
struct Factory:host::HostExecutionFactoryPort {
  std::shared_ptr<host::HostExecutionPort> backend;
  Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id)override {
    auto made=ock::cpu_pool::Executor::create({2,2});CHECK(made);std::shared_ptr<ExecutorControlPort> pool=std::move(*made);
    host::ExecutionOptions options;options.subjects={{policy_test::principal().principal_id,1,1}};
    auto value=host::make_executions(id,pool,options);if(!value){(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));return value;}
    backend=*value;return value;
  }
};
struct Lifecycle:host::ModuleLifecyclePort {
  bool stopped=false;
  Result<void> start(const host::ModuleContext&)override{return {};}
  host::ModuleStopResult stop()override{stopped=true;return {true,{}};}
};
struct ObservationThreads:TrustedThreadPort {
  std::thread::id application=std::this_thread::get_id();
  Result<ThreadObservation> current()const noexcept override {
    return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
  }
};
}
void host_observation_drain() {
  std::optional<registry::ModuleInput> registration;
  Env env(false,compute,{},[&](registry::ModuleInput& module){
    module.register_operations=[](registry::Registrar& registrar){
      registry::SubmissionStorage<int,int> storage{sizeof(int),sizeof(InvokeReply<int>),
          [](const int&)->Result<std::size_t>{return sizeof(int);},
          [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
      CHECK(registrar.compute(compute,{key(),{},{false,false,false,name("test"),name("app")},AtomicMode::PureCompute,{name("allow")},"observe"},{{},{},{name("native"),name("test")},{},false},storage));
    };registration=module;
  });
  auto threads=std::make_shared<ObservationThreads>();
  auto factory=std::make_shared<Factory>();auto lifecycle=std::make_shared<Lifecycle>();
  auto made=host::NativeHost::create({},policy_test::configuration(),{env.policy.auth,env.policy.clock,env.policy.digest,threads,{},factory});CHECK(made);auto& host=*made;
  CHECK(host->add({*registration,lifecycle}));CHECK(host->start());
  auto session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
  auto caller=session->verify({policy_test::principal(),{},{}});CHECK(caller);
  auto source=factory->backend->observation_events();CHECK(source);
  auto barrier=std::make_shared<Barrier>();auto arrived=barrier->entered.get_future();
  auto receiver=std::make_shared<BlockingReceiver>(barrier,[&]{return host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant;});
  auto lease=source->observe_changes((*caller)->view(),{{},policy_test::principal(),{ObservationTopic::Phase}},receiver);CHECK(lease);receiver.reset();
  auto bound=session->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("observe.submit"));CHECK(bound);
  auto submitted=bound->submit(1,env.options());CHECK(std::holds_alternative<Accepted>(submitted));
  auto waited=session->wait(**caller,std::get<Accepted>(submitted).execution,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
  auto pumping=std::async(std::launch::async,[source]{return source->pump(1);});Release release{barrier};
  CHECK(arrived.wait_for(std::chrono::seconds(2))==std::future_status::ready&&barrier->reentrant);
  auto timeout=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(200));CHECK(!timeout.quiescent&&!lifecycle->stopped);
  CHECK(!source->observe_changes((*caller)->view(),{{},policy_test::principal(),{ObservationTopic::Phase}},std::make_shared<Receiver>()));
  barrier->release.set_value();CHECK(pumping.get());lease->reset();CHECK(barrier->destroyed);
  auto closed=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(closed.quiescent&&lifecycle->stopped);
}
#endif
}
