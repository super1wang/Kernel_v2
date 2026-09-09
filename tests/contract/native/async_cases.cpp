#include "fixtures.hpp"
#include <mutex>
#include <barrier>
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/runtime/host.hpp>
namespace native_test {
namespace {
struct AsyncReader {int read() const noexcept {return 7;}};
using AsyncCall=AsyncReadCall<Own,int,AsyncReader>;
std::mutex async_mutex;
std::shared_ptr<AsyncCall> async_pending;
std::atomic<unsigned> async_entered=0;
std::atomic<bool> async_duplicate=false;
std::function<void(WorkContext&)> async_children;
std::function<void()> async_measure_hook;
void hold_async(std::shared_ptr<AsyncCall> call) {std::lock_guard lock(async_mutex);async_pending=std::move(call);}
std::shared_ptr<AsyncCall> pending_async() {std::lock_guard lock(async_mutex);return async_pending;}
void release_async() {
  std::shared_ptr<AsyncCall> released;
  {std::lock_guard lock(async_mutex);released=std::move(async_pending);}
}
Result<void> async_handler(std::shared_ptr<AsyncCall> call) {
  ++async_entered;CHECK(call->work().execution_scope());CHECK(call->work().granted_resources().size()==1);
  CHECK(call->input().text.size()==1024);
  switch(call->input().text[0]) {
    case 'i': {
      CHECK(call->complete(call->reader().read()));
      auto duplicate=call->complete(99);
      async_duplicate=!duplicate&&duplicate.error().code()==error(ContractsErrc::DuplicateCompletion).code();
      return {};
    }
    case 'm':return {}; // 未 complete 就放弃最后 owner，必须有可靠失败回执。
    case 't':throw std::runtime_error("async start failure");
    case 'e':CHECK(call->complete(call->reader().read()));throw std::runtime_error("after completion");
    case 'f':return make_unexpected(error(ContractsErrc::Rejected));
    case 'h':CHECK(call->complete(call->reader().read()));hold_async(std::move(call));return {};
    case 'p':CHECK(bool(async_children));async_children(call->work());hold_async(std::move(call));return {};
    default:hold_async(std::move(call));return {};
  }
}
template<class Predicate> void async_until(Predicate predicate) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  CHECK(predicate());
}
struct AsyncHost {
  struct Factory final : host::HostExecutionFactoryPort {
    std::shared_ptr<RequiredRecordPort> recorder;
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      auto made=ock::cpu_pool::Executor::create({2,2});CHECK(made);
      std::shared_ptr<ExecutorControlPort> pool=std::move(*made);
      host::ExecutionOptions options;options.children_per_execution=1;
      options.required_record=recorder;
      options.subjects={{policy_test::principal().principal_id}};
      options.slots={{"shared.slot",1,false}};options.aliases={{"slot.alias","shared.slot"}};
      options.resources={{{name("native"),name("declared")},{{"slot.alias",resources::Mode::Exclusive,1}}}};
      auto backend=host::make_executions(id,pool,options);
      if(!backend)CHECK(pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));
      return backend;
    }
  };
  struct Lifecycle final : host::ModuleLifecyclePort {
    Result<void> start(const host::ModuleContext&) override {return {};}
    host::ModuleStopResult stop() override {return {true,{}};}
  };
  struct Threads final : TrustedThreadPort {
    std::thread::id application=std::this_thread::get_id();
    Result<ThreadObservation> current() const noexcept override {
      return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
    }
  };
  std::shared_ptr<AsyncReader> reader=std::make_shared<AsyncReader>();
  std::optional<registry::ModuleInput> registration;
  Env env;
  std::unique_ptr<host::NativeHost> host;
  std::optional<host::HostSession> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  explicit AsyncHost(std::shared_ptr<RequiredRecordPort> recorder={}):env(true,compute,{},[&](registry::ModuleInput& module) {
    module.services.clear();module.services.push_back(*registry::ServiceBinding::make(name("reader"),reader));
    module.manifest.required_services[0].type=CppTypeToken::of<AsyncReader>();
    module.manifest.resources.push_back(name("declared"));
    module.manifest.required_resources.push_back({name("native"),name("declared")});
    module.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
    module.executors[0].async_dispatch=true;module.executors[0].external_wait=true;
    module.register_operations=[](registry::Registrar& registrar) {
      DefinitionInput definition{key(),{}, {false,true,true,name("test"),name("app")},
          AtomicMode::Incompatible,{name("allow")},"async.read"};
      registry::OperationOptions options{{{name("native"),name("reader")}},{},{name("native"),name("test")},
          {{name("native"),name("declared")}},false};
      registry::SubmissionStorage<Own,int> storage{4096,sizeof(InvokeReply<int>),
          [](const Own& input)->Result<std::size_t>{return sizeof(Own)+input.text.capacity();},
          [](const InvokeReply<int>&)->Result<std::size_t>{if(async_measure_hook)async_measure_hook();return sizeof(InvokeReply<int>);}};
      CHECK(registrar.read_async(async_handler,definition,options,storage));
    };
    registration=module;
  }) {
    auto factory=std::make_shared<Factory>();factory->recorder=std::move(recorder);
    host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<Threads>(),{},factory};
    auto made=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(made);host=std::move(*made);
    CHECK(host->add({*registration,std::make_shared<Lifecycle>()}));CHECK(host->start());
    auto opened=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(opened);
    session.emplace(std::move(*opened));
    auto verified=session->verify({policy_test::principal(),{}, {}});CHECK(verified);caller=*verified;
    env.engine.reset();env.catalog.reset();registration.reset();reader.reset();
  }
  static Result<std::size_t> project(const Own&,std::span<foundation::ObjectId> targets) noexcept {
    if(targets.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));
    targets[0]=policy_test::target();return 1;
  }
  auto bind() {return session->bind<Own,int>(key(),{},Shape::Read,caller,
      std::array{policy_test::target()},project,name("async.read"));}
  auto options() {return invocation::InvokeOptions{{},env.policy.clock->now()+std::chrono::seconds(4),100};}
  ExecutionPhase phase(ExecutionRef ref) {
    auto context=session->catalog_context();CHECK(context);
    auto observed=context->authorization->observations()->get(*caller,ref,policy::AccessUse::GetSummary);CHECK(observed);
    return observed->summary->value().phase;
  }
  auto wait(ExecutionRef ref) {
    return session->wait(*caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2),{});
  }
  ~AsyncHost() {
    release_async();
    if(host)(void)host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
  }
};
Own async_input(char mode) {return {std::string(1024,mode)};}
ExecutionRef accepted_ref(const SubmitReply& reply) {CHECK(std::holds_alternative<Accepted>(reply));return std::get<Accepted>(reply).execution;}
struct TestRecorder final : RequiredRecordPort {
  char mode='s';std::atomic<unsigned> calls=0,reservations=0;
  std::mutex mutex;std::shared_ptr<RecordReceiver> held;
  std::shared_ptr<const RecordRequestSnapshot> request;
  std::function<void()> hook;
  Result<std::unique_ptr<RecordReservation>> reserve(const RecordRequest& r) override {
    ++reservations;CHECK(r.capacity==256&&r.receipt.empty());
    if(mode=='r')return make_unexpected(error(ContractsErrc::BudgetExceeded));
    return std::unique_ptr<RecordReservation>(new RecordReservation);
  }
  Result<void> record(std::unique_ptr<RecordReservation> reservation,
      std::shared_ptr<const RecordRequestSnapshot> r,std::shared_ptr<RecordReceiver> receiver) override {
    CHECK(reservation&&r);++calls;if(hook)hook();
    if(mode=='t')throw std::runtime_error("record failure");
    if(mode=='m')return {};
    if(mode=='h'||mode=='p') {std::lock_guard lock(mutex);held=receiver;request=r;}
    if(mode=='p')return {};
    RecordReport report{r->value().execution,{},{},RequiredRecordState::Recorded,{}};
    if(mode=='f') {report.state=RequiredRecordState::Failed;report.failure=RecordFailure{error(ContractsErrc::Rejected),true,RepairKind::ManualReview};}
    if(mode=='b')report.execution.execution_id={};
    receiver->completed(report);
    if(mode=='d') {report.state=RequiredRecordState::Failed;report.failure=RecordFailure{error(ContractsErrc::Rejected),true,RepairKind::ManualReview};receiver->completed(report);}
    if(mode=='e')return make_unexpected(error(ContractsErrc::Rejected));
    if(mode=='l')throw std::runtime_error("after record report");
    return {};
  }
  void release() {
    std::shared_ptr<RecordReceiver> old;std::shared_ptr<const RecordRequestSnapshot> snapshot;
    {std::lock_guard lock(mutex);old=std::move(held);snapshot=std::move(request);}
  }
  bool holding() {std::lock_guard lock(mutex);return bool(held);}
  void fail_held() {
    std::shared_ptr<RecordReceiver> receiver;std::shared_ptr<const RecordRequestSnapshot> snapshot;
    {std::lock_guard lock(mutex);receiver=held;snapshot=request;}
    CHECK(receiver&&snapshot);
    receiver->completed({snapshot->value().execution,{},{},RequiredRecordState::Failed,
        RecordFailure{error(ContractsErrc::Rejected),true,RepairKind::ManualReview}});
  }
};
}
void host_async_completion() {
  release_async();async_entered=0;async_duplicate=false;AsyncHost fixture;
  auto first=fixture.bind(),second=fixture.bind();CHECK(first&&second);
  auto invoked=first->invoke(async_input('i'),fixture.options());CHECK(std::holds_alternative<Rejected>(invoked));
  CHECK(std::get<Rejected>(invoked).reason.code()==invocation_error(InvocationErrc::SubmitRequired).code());
  Own input=async_input('d');auto ref=accepted_ref(first->submit(input,fixture.options()));
  input.text.clear();input.text.shrink_to_fit();
  async_until([]{return bool(pending_async());});auto call=pending_async();
  CHECK(call->input().text==std::string(1024,'d'));CHECK(call->reader().read()==7);
  auto other=accepted_ref(second->submit(async_input('i'),fixture.options()));
  async_until([&]{return fixture.phase(other)==ExecutionPhase::WaitingResources;});CHECK(async_entered==1);
  CHECK(call->complete(call->reader().read()));
  async_until([&]{return fixture.phase(ref)==ExecutionPhase::Finalizing;});
  CHECK(call->input().text==std::string(1024,'d'));CHECK(call->work().granted_resources().size()==1);
  CHECK(fixture.phase(other)==ExecutionPhase::WaitingResources);CHECK(async_entered==1);
  CHECK(!fixture.session->result<int>(*fixture.caller,ref));
  auto timeout=fixture.session->wait(*fixture.caller,ref,std::chrono::steady_clock::now(),{});
  CHECK(timeout&&timeout->state==host::ExecutionWaitState::Timeout);
  release_async();call.reset();
  auto done=fixture.wait(ref);CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  auto result_one=fixture.session->result<int>(*fixture.caller,ref);CHECK(result_one&&result(*result_one->value)==7);
  done=fixture.wait(other);CHECK(done&&done->state==host::ExecutionWaitState::Terminal);CHECK(async_duplicate);
  for(char mode:{'m','t','e','f'}) {
    auto id=accepted_ref(first->submit(async_input(mode),fixture.options()));
    auto terminal=fixture.wait(id);CHECK(terminal&&terminal->state==host::ExecutionWaitState::Terminal);
    auto reply=fixture.session->result<int>(*fixture.caller,id);CHECK(reply);
    if(mode=='e')CHECK(result(*reply->value)==7);
    else CHECK(terminal->observed.summary->value().fault.has_value());
  }
  auto concurrent=accepted_ref(first->submit(async_input('d'),fixture.options()));
  async_until([]{return bool(pending_async());});call=pending_async();
  std::atomic<unsigned> winners=0,duplicates=0;std::barrier gate(3);
  auto complete=[&](int value) {
    gate.arrive_and_wait();auto reply=call->complete(value);
    if(reply)++winners;
    else if(reply.error().code()==error(ContractsErrc::DuplicateCompletion).code())++duplicates;
  };
  std::thread a(complete,7),b(complete,8);gate.arrive_and_wait();a.join();b.join();
  CHECK(winners==1&&duplicates==1);
  release_async();call.reset();CHECK(fixture.wait(concurrent));
  auto winning=fixture.session->result<int>(*fixture.caller,concurrent);CHECK(winning);
  CHECK(result(*winning->value)==7||result(*winning->value)==8);
  auto reentrant=accepted_ref(first->submit(async_input('d'),fixture.options()));
  async_until([]{return bool(pending_async());});auto raw=pending_async().get();
  async_measure_hook=[&] {
    auto waited=fixture.session->wait(*fixture.caller,reentrant,std::chrono::steady_clock::now()+std::chrono::seconds(1),{});
    CHECK(!waited&&waited.error().code()==invocation_error(InvocationErrc::ThreadRejected).code());
    auto closed=fixture.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
    CHECK(closed.disposition==host::ShutdownDisposition::Reentrant);
    release_async();
  };
  CHECK(raw->complete(7)); // complete 内部须保活真实 owner，不能返回到已销毁的 source。
  async_measure_hook={};CHECK(!pending_async());
  auto final=fixture.wait(reentrant);CHECK(final&&final->state==host::ExecutionWaitState::Terminal);
  auto stopped=accepted_ref(first->submit(async_input('d'),fixture.options()));
  async_until([]{return bool(pending_async());});
  std::weak_ptr<AsyncCall> weak=pending_async();auto token=pending_async()->work().stop_token();
  std::atomic<bool> callback=false;
  std::stop_callback on_stop(token,[weak,&callback] {
    auto call=weak.lock();CHECK(call);
    CHECK(call->complete(make_unexpected(invocation_error(InvocationErrc::Cancelled))));
    release_async();callback=true;
  });
  CHECK(fixture.session->cancel(*fixture.caller,stopped));CHECK(callback);
  final=fixture.wait(stopped);CHECK(final&&final->state==host::ExecutionWaitState::Terminal);
  CHECK(final->observed.summary->value().fault.has_value());CHECK(weak.expired());
}
void host_async_drain() {
  release_async();async_entered=0;AsyncHost fixture;
  auto parent=fixture.bind(),child=fixture.bind();CHECK(parent&&child);
  std::atomic<unsigned> rejected_children=0;
  async_children=[&](WorkContext& work) {
    // 两次均到实际规范化 Lease 检查；第一次失败须回收唯一父槽。
    for(unsigned i=0;i<2;++i) {
      auto reply=child->submit_child(work,async_input('i'),fixture.options());CHECK(std::holds_alternative<Rejected>(reply));
      CHECK(std::get<Rejected>(reply).reason.code()==resources::error(resources::Errc::UnsafeChildWait).code());
      ++rejected_children;
    }
  };
  auto ref=accepted_ref(parent->submit(async_input('p'),fixture.options()));
  async_until([]{return bool(pending_async());});CHECK(rejected_children==2);auto call=pending_async();
  auto cancel=fixture.session->cancel(*fixture.caller,ref);CHECK(cancel&&*cancel==CancelDisposition::AlreadyClaimed);
  CHECK(call->work().stop_requested());CHECK(fixture.phase(ref)!=ExecutionPhase::Terminal);
  CHECK(call->complete(call->reader().read()));
  async_until([&]{return fixture.phase(ref)==ExecutionPhase::Finalizing;});
  CHECK(!fixture.host->shutdown_until(std::chrono::steady_clock::now()).quiescent);
  CHECK(call->input().text==std::string(1024,'p'));CHECK(call->reader().read()==7);
  release_async();call.reset();
  CHECK(fixture.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)).quiescent);
  async_children={};
}
void host_async_deadline() {
  release_async();AsyncHost fixture;auto bound=fixture.bind();CHECK(bound);
  auto options=fixture.options();options.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
  auto ref=accepted_ref(bound->submit(async_input('d'),options));
  async_until([]{return bool(pending_async());});auto call=pending_async();
  std::atomic<bool> stopped=false;
  std::stop_callback callback(call->work().stop_token(),[&]{stopped=true;});
  async_until([&]{return stopped.load();});CHECK(call->work().stop_requested());
  CHECK(fixture.phase(ref)!=ExecutionPhase::Terminal);
  CHECK(call->input().text==std::string(1024,'d'));CHECK(call->reader().read()==7);
  CHECK(call->complete(7)); // 到期不能覆盖已经发生的读结果，owner 尚在仍不得早终态。
  async_until([&]{return fixture.phase(ref)==ExecutionPhase::Finalizing;});
  release_async();call.reset();auto done=fixture.wait(ref);
  CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  auto value=fixture.session->result<int>(*fixture.caller,ref);CHECK(value&&result(*value->value)==7);
}
void host_required_record() {
  for(char mode:{'s','d','e','l','f','m','t','b'}) {
    auto recorder=std::make_shared<TestRecorder>();recorder->mode=mode;AsyncHost fixture(recorder);
    auto bound=fixture.bind();CHECK(bound);
    auto ref=accepted_ref(bound->submit(async_input('i'),fixture.options()));
    auto done=fixture.wait(ref);CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
    CHECK(recorder->calls==1&&recorder->reservations==1);
    auto reply=fixture.session->result<int>(*fixture.caller,ref);CHECK(reply&&result(*reply->value)==7);
    const auto& summary=done->observed.summary->value();
    const auto& outcome=std::get<Completed<int>>(*reply->value).outcome;
    bool failed=mode!='s'&&mode!='d'&&mode!='e'&&mode!='l';
    CHECK(summary.record_state==(failed?RequiredRecordState::Failed:RequiredRecordState::Recorded));
    CHECK(summary.evidence==outcome.evidence());CHECK(summary.writes_blocked==failed);
    CHECK(outcome.conditions().finalization.record_state==summary.record_state);
    if(failed) {
      CHECK(summary.fault&&summary.repair==RepairKind::ManualReview);
      CHECK(std::holds_alternative<Rejected>(bound->submit(async_input('i'),fixture.options())));
      CHECK(recorder->calls==1); // 门控关闭后不再次运行业务或必要记录。
    }
  }
  auto recorder=std::make_shared<TestRecorder>();recorder->mode='r';AsyncHost fixture(recorder);
  auto bound=fixture.bind();CHECK(bound);auto before=async_entered.load();
  CHECK(std::holds_alternative<Rejected>(bound->submit(async_input('i'),fixture.options())));
  CHECK(async_entered==before&&recorder->calls==0);
  auto queued_recorder=std::make_shared<TestRecorder>();AsyncHost queued(queued_recorder);
  auto first=queued.bind(),second=queued.bind();CHECK(first&&second);
  auto running=accepted_ref(first->submit(async_input('d'),queued.options()));
  async_until([]{return bool(pending_async());});auto call=pending_async();
  auto waiting=accepted_ref(second->submit(async_input('i'),queued.options()));
  async_until([&]{return queued.phase(waiting)==ExecutionPhase::WaitingResources;});
  CHECK(queued.session->cancel(*queued.caller,waiting));
  auto done=queued.wait(waiting);CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  CHECK(done->observed.summary->value().record_state==RequiredRecordState::Recorded);
  auto rejected=queued.session->result<int>(*queued.caller,waiting);CHECK(rejected&&std::holds_alternative<Rejected>(*rejected->value));
  CHECK(call->complete(7));release_async();call.reset();CHECK(queued.wait(running));
}
void host_required_record_drain() {
  auto recorder=std::make_shared<TestRecorder>();recorder->mode='h';AsyncHost fixture(recorder);
  recorder->hook=[&] {
    CHECK(fixture.host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant);
  };
  auto bound=fixture.bind();CHECK(bound);
  auto ref=accepted_ref(bound->submit(async_input('i'),fixture.options()));
  async_until([&]{return recorder->holding()&&fixture.phase(ref)==ExecutionPhase::Finalizing;});
  CHECK(!fixture.session->result<int>(*fixture.caller,ref));
  CHECK(!fixture.host->shutdown_until(std::chrono::steady_clock::now()).quiescent);
  recorder->release();
  CHECK(fixture.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)).quiescent);
  auto failed_recorder=std::make_shared<TestRecorder>();failed_recorder->mode='p';AsyncHost failed_fixture(failed_recorder);
  auto failed_bound=failed_fixture.bind();CHECK(failed_bound);
  auto failed_ref=accepted_ref(failed_bound->submit(async_input('i'),failed_fixture.options()));
  async_until([&]{return failed_recorder->holding();});failed_recorder->fail_held();
  auto context=failed_fixture.session->catalog_context();CHECK(context);
  async_until([&] {
    auto summary=context->authorization->observations()->get(*failed_fixture.caller,failed_ref,policy::AccessUse::GetSummary);
    return summary&&summary->summary->value().record_state==RequiredRecordState::Failed;
  });
  CHECK(failed_fixture.phase(failed_ref)==ExecutionPhase::Finalizing);
  CHECK(std::holds_alternative<Rejected>(failed_bound->submit(async_input('i'),failed_fixture.options())));
  failed_recorder->release();auto done=failed_fixture.wait(failed_ref);CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  auto value=failed_fixture.session->result<int>(*failed_fixture.caller,failed_ref);CHECK(value&&result(*value->value)==7);
}
}
#endif
