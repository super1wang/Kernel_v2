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
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      auto made=ock::cpu_pool::Executor::create({2,2});CHECK(made);
      std::shared_ptr<ExecutorControlPort> pool=std::move(*made);
      host::ExecutionOptions options;options.children_per_execution=1;
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
  AsyncHost():env(true,compute,{},[&](registry::ModuleInput& module) {
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
    host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<Threads>(),{},std::make_shared<Factory>()};
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
}
#endif
