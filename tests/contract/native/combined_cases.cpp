#include "fixtures.hpp"
#include "packages/runtime/executions/host_execution.hpp"
#include <future>
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
namespace native_test {
namespace {
struct Barrier {
  std::promise<void> entered,release;
  std::shared_future<void> released=release.get_future().share();
  void open()noexcept{try{release.set_value();}catch(...) {}}
};
struct Stress {
  Barrier work,observation;
  std::atomic<unsigned> handlers=0;
  std::atomic<bool> resource_seen=false,worker_reentrant=false,observer_reentrant=false,observer_destroyed=false;
  host::NativeHost* host=nullptr;
};
std::shared_ptr<Stress> current;
Result<int> handler(const int&,WorkContext& context) {
  std::cerr<<"combined handler entry"<<std::endl;
  auto state=current;++state->handlers;
  state->resource_seen=context.granted_resources().size()==1&&bool(context.granted_resources()[0]);
  state->worker_reentrant=state->host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant;
  std::cerr<<"combined handler reentrant "<<state->worker_reentrant<<" resource "<<state->resource_seen<<std::endl;
  state->work.entered.set_value();state->work.released.wait();
  throw std::runtime_error("combined business callback fault");
}
struct Receiver:ObservationReceiver {
  std::shared_ptr<Stress> state;
  explicit Receiver(std::shared_ptr<Stress> s):state(std::move(s)){}
  ~Receiver()override{state->observer_destroyed=true;}
  void changed(ChangeHint)noexcept override {
    state->observer_reentrant=state->host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant;
    state->observation.entered.set_value();state->observation.released.wait();
  }
};
struct Release {std::shared_ptr<Stress> state;std::function<void()> cleanup;~Release(){state->work.open();state->observation.open();cleanup();}};
struct SlowLog:LogPort {
  std::shared_ptr<LogPort> inner;
  bool allow_close=false;
  bool congested=false;
  unsigned busy_writes=0;
  unsigned close_attempts=0;
  explicit SlowLog(std::shared_ptr<LogPort> value):inner(std::move(value)){}
  LogWriteResult try_write(const LogInput& input)override {
    if(congested){++busy_writes;return {LogDecision::Rejected,LogReason::Busy,{}};}
    return inner->try_write(input);
  }
  Result<LogSnapshot> snapshot()override {
    auto value=inner->snapshot();if(value){value->counters.rejected+=busy_writes;value->counters.rejected_busy+=busy_writes;}return value;
  }
  Result<LogFlushResult> flush(LogPosition through)override{return inner->flush(through);}
  Result<LogFlushResult> close()override {
    ++close_attempts;
    if(!allow_close)return make_unexpected(log_error(LogErrc::Busy));
    return inner->close();
  }
};
struct LogFactory:host::HostLogFactoryPort {
  std::shared_ptr<SlowLog> log;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation id,const LogLimits& limits)override {
    auto made=observability::make_memory_logging(id,1,limits);CHECK(made);
    log=std::make_shared<SlowLog>(made->writer);return std::shared_ptr<LogPort>(log);
  }
};
struct Factory:host::HostExecutionFactoryPort {
  std::shared_ptr<executions::detail::HostedExecutions> backend;
  std::shared_ptr<ExecutorControlPort> pool;
  Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id)override {
    auto made=ock::cpu_pool::Executor::create({2,8});CHECK(made);pool=std::move(*made);
    host::ExecutionOptions options;options.active=4;options.subjects={{policy_test::principal().principal_id,1,2}};
    options.slots={{"combined.slot",1,false}};
    options.resources={{{name("native"),name("declared")},{{"combined.slot",resources::Mode::Exclusive,1}}}};
    auto value=host::make_executions(id,pool,options);
    if(!value){(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));return value;}
    backend=std::dynamic_pointer_cast<executions::detail::HostedExecutions>(*value);CHECK(backend);return value;
  }
};
struct Lifecycle:host::ModuleLifecyclePort {
  bool stopped=false;
  Result<void> start(const host::ModuleContext&)override{return {};}
  host::ModuleStopResult stop()override{stopped=true;return {true,{}};}
};
struct CombinedThreads:TrustedThreadPort {
  std::thread::id application=std::this_thread::get_id();
  Result<ThreadObservation> current()const noexcept override {
    return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
  }
};
}
void host_combined_drain() {
  for(unsigned round=0;round<8;++round) {
    auto mark=[&](const char* phase){std::cerr<<"combined "<<round<<' '<<phase<<std::endl;};mark("begin");
    auto state=std::make_shared<Stress>();current=state;
    auto work_entered=state->work.entered.get_future(),observer_entered=state->observation.entered.get_future();
    std::optional<registry::ModuleInput> registration;
    Env env(false,handler,{},[&](registry::ModuleInput& module){
      module.manifest.resources.push_back(name("declared"));
      module.manifest.required_resources.push_back({name("native"),name("declared")});
      module.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
      module.register_operations=[](registry::Registrar& registrar){
        registry::SubmissionStorage<int,int> storage{sizeof(int),sizeof(InvokeReply<int>),
          [](const int&)->Result<std::size_t>{return sizeof(int);},
          [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
        CHECK(registrar.compute(handler,{key(),{},{false,false,false,name("test"),name("app")},AtomicMode::PureCompute,{name("allow")},"combined"},
          {{},{},{name("native"),name("test")},{{name("native"),name("declared")}},false},storage));
      };registration=module;
    });
    auto factory=std::make_shared<Factory>();auto logging=std::make_shared<LogFactory>();auto lifecycle=std::make_shared<Lifecycle>();
    auto made=host::NativeHost::create({},policy_test::configuration(),{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<CombinedThreads>(),logging,factory});CHECK(made);
    auto& host=*made;state->host=host.get();
    std::future<Result<std::size_t>> pumping;Release release{state,[logging]{if(logging->log)logging->log->allow_close=true;}};
    CHECK(host->add({*registration,lifecycle}));CHECK(host->start());
    mark("ready");
    logging->log->congested=true;
    auto session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
    auto caller=session->verify({policy_test::principal(),{},{}});CHECK(caller);
    mark("session-open");
    auto bind=[&]{return session->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("combined"));};
    auto first=bind(),second=bind();CHECK(first&&second);
    mark("bound");
    auto source=factory->backend->observation_events();
    auto receiver=std::make_shared<Receiver>(state);
    auto lease=source->observe_changes((*caller)->view(),{{},policy_test::principal(),{ObservationTopic::Phase}},receiver);CHECK(lease);receiver.reset();
    mark("observer-installed");
    InvokeOptions options{{},std::chrono::steady_clock::now()+std::chrono::seconds(10),100};
    auto one=first->submit(1,options);CHECK(std::holds_alternative<Accepted>(one));auto one_ref=std::get<Accepted>(one).execution;
    mark("submitted");
    CHECK(work_entered.wait_for(std::chrono::seconds(2))==std::future_status::ready&&state->resource_seen&&state->worker_reentrant);
    mark("first-entered");
    auto two=second->submit(2,options);CHECK(std::holds_alternative<Accepted>(two));auto two_ref=std::get<Accepted>(two).execution;
    auto table=factory->backend->table();auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);bool waiting=false;
    while(std::chrono::steady_clock::now()<until) {
      auto value=table->access_find(two_ref);CHECK(value);
      if(value->summary->value().phase==ExecutionPhase::WaitingResources){waiting=true;break;}
      std::this_thread::yield();
    }
    CHECK(waiting&&state->handlers==1);
    mark("resource-wait");
    auto report=std::make_shared<WorkReport>();
    CHECK(factory->pool->submit(std::make_unique<CallbackWork>([]{return Result<void>{};},[](Result<void>){throw std::runtime_error("combined completion fault");},report)));
    until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(report->callback_exceptions!=1&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    CHECK(report->executions==1&&report->callback_exceptions==1&&state->handlers==1);
    mark("callback-fault-isolated");
    // 注册后的真实执行变化触发观察回调；不制造虚构状态或通知。
    pumping=std::async(std::launch::async,[source]{return source->pump(1);});
    CHECK(observer_entered.wait_for(std::chrono::seconds(2))==std::future_status::ready&&state->observer_reentrant);
    mark("observer-entered");
    std::array<host::PendingCleanup,8> pending;
    auto stopped=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(20),pending);
    CHECK(!stopped.quiescent&&!lifecycle->stopped&&logging->log->close_attempts==0&&state->handlers==1);
    CHECK(logging->log->busy_writes>0);
    mark("first-stop-timeout");
    state->work.open();
    stopped=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(100),pending);
    CHECK(!stopped.quiescent&&!lifecycle->stopped&&logging->log->close_attempts==0);
    mark("observer-stop-timeout");
    CHECK(state->handlers==1);
    auto failed=table->result<int>(one_ref);CHECK(failed);
    const auto* completed=std::get_if<Completed<int>>(failed->get());CHECK(completed);
    const auto* outcome=std::get_if<FailedBeforeApply>(&completed->outcome.value());CHECK(outcome);
    CHECK(outcome->reason.code()==invocation_error(InvocationErrc::HandlerException).code());
    mark("outcome-verified");
    CHECK(table->access_find(two_ref)->summary->value().phase==ExecutionPhase::Terminal);
    state->observation.open();CHECK(pumping.get());lease->reset();CHECK(state->observer_destroyed);
    mark("observer-released");
    stopped=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),pending);
    CHECK(!stopped.quiescent&&lifecycle->stopped&&logging->log->close_attempts==1);
    mark("logging-pending");
    CHECK(std::any_of(pending.begin(),pending.begin()+stopped.pending_written,[](const auto& item){return item.kind==host::PendingKind::Logging;}));
    CHECK(factory->backend->service().active()==0&&factory->backend->service().scheduler_snapshot().worker_delivery==0);
    logging->log->allow_close=true;
    stopped=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),pending);
    CHECK(stopped.quiescent&&stopped.pending_total==0&&stopped.disposition==host::ShutdownDisposition::CompleteWithErrors);
    mark("quiescent");
    CHECK(logging->log->close_attempts==2&&factory->pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)));
    CHECK(std::holds_alternative<Rejected>(first->submit(3,options)));
    current.reset();
  }
}
}
#endif
