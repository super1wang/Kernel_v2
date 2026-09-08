#include <ock/runtime/host.hpp>
#include "tests/contract/native/fixtures.hpp"
#include "tests/contract/native/pipeline_cases.hpp"
#include "tests/contract/native/value_cases.hpp"
#include "tests/contract/native/policy_inline_cases.hpp"
#include "tests/contract/native/allocation_cases.hpp"
#include <condition_variable>
#include <mutex>
#include <crtdbg.h>

struct HostMoveValue {
  int value;
  static inline void(*moving)() noexcept=nullptr;
  explicit HostMoveValue(int v):value(v){}
  HostMoveValue(HostMoveValue&& other) noexcept:value(other.value){if(moving)moving();}
  HostMoveValue& operator=(HostMoveValue&&) noexcept=default;
  HostMoveValue(const HostMoveValue&)=delete;
};
namespace ock::contracts {
template<> struct TypeContract<HostMoveValue> {
  static TypeIdentity identity(){return {*Name::parse("host.move"),*OperationVersion::parse("1.0.0",128),{}};}
  static Result<void> validate(const HostMoveValue& value){return value.value>=0?Result<void>{}:reject(ContractsErrc::InvalidContract);}
  static constexpr auto async_ownership=AsyncOwnership::Owning;
};
}

namespace host_test {
using namespace ock::runtime;
using namespace host;

struct Ports {
  std::shared_ptr<policy_test::Clock> clock = std::make_shared<policy_test::Clock>();
  std::shared_ptr<policy_test::Auth> auth = std::make_shared<policy_test::Auth>(clock->now()+std::chrono::hours(1));
  std::shared_ptr<policy_test::Digest> digest = std::make_shared<policy_test::Digest>();
  std::shared_ptr<native_test::Threads> threads = std::make_shared<native_test::Threads>();
  HostPorts value() const { return {auth,clock,digest,threads,{}}; }
};
struct Lifecycle final : ModuleLifecyclePort {
  std::vector<int>& events;
  int number;
  bool fail_start=false,fail_stop=false;
  std::function<void(const ModuleContext&)> on_start;
  std::function<ModuleStopResult()> on_stop;
  explicit Lifecycle(std::vector<int>& e,int n):events(e),number(n) {}
  Result<void> start(const ModuleContext& context) override {
    events.push_back(number);
    if(on_start)on_start(context);
    if(fail_start)return make_unexpected(host_error(HostErrc::InvalidInput));
    return {};
  }
  ModuleStopResult stop() override {
    events.push_back(-number);
    if(on_stop)return on_stop();
    return {!fail_stop,{}};
  }
};
HostModule module(const char* text,std::shared_ptr<Lifecycle> lifecycle,const char* dependency=nullptr) {
  registry::ModuleInput input{registry::ModuleManifest{name(text),ver()}};
  if(dependency)input.manifest.dependencies.push_back({name(dependency),ver()});
  input.register_operations=[](registry::Registrar&){};
  return {std::move(input),std::move(lifecycle)};
}
struct ForeignLogging final : HostLogFactoryPort {
  std::shared_ptr<LogPort> owner;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation,const LogLimits& limits) override {
    auto made=observability::make_memory_logging(id<HostIncarnation>(19),1,limits);
    CHECK(made);owner=made->writer;return owner;
  }
};
struct ReentrantLogging final : HostLogFactoryPort {
  NativeHost* host=nullptr;
  bool refused=false,not_ready=false;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation id,const LogLimits& limits) override {
    auto report=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
    refused=report.disposition==ShutdownDisposition::Reentrant&&!report.quiescent;
    auto opened=host->open({},{});
    not_ready=!opened&&opened.error().code()==host_error(HostErrc::NotReady).code();
    auto made=observability::make_memory_logging(id,1,limits);CHECK(made);return made->writer;
  }
};
struct ControlledLog final : LogPort {
  std::shared_ptr<LogPort> inner;
  TimePoint delay_until{};
  TimePoint close_delay{};
  unsigned close_calls=0,write_calls=0;
  bool fail_close=false,throw_close=false,throw_write=false,fail_snapshot=false,throw_snapshot=false;
  LogWriteResult try_write(const LogInput& input) override {
    ++write_calls;if(throw_write)throw std::runtime_error("private log sentinel");
    if(input.event==LogEvent::LogClosing&&delay_until!=TimePoint{})std::this_thread::sleep_until(delay_until);
    return inner->try_write(input);
  }
  Result<LogSnapshot> snapshot() override {
    if(throw_snapshot)throw std::runtime_error("private snapshot sentinel");
    if(fail_snapshot)return make_unexpected(log_error(LogErrc::BackendFailure));
    return inner->snapshot();
  }
  Result<LogFlushResult> flush(LogPosition p) override {return inner->flush(p);}
  Result<LogFlushResult> close() override {
    if(close_delay!=TimePoint{})std::this_thread::sleep_until(close_delay);
    ++close_calls;if(throw_close)throw std::runtime_error("private close sentinel");
    if(fail_close)return make_unexpected(log_error(LogErrc::BackendFailure));return inner->close();
  }
};
struct ControlledFactory final : HostLogFactoryPort {
  std::shared_ptr<ControlledLog> log=std::make_shared<ControlledLog>();
  unsigned calls=0;
  int mode=0;
  int limits_mismatch=0;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation id,const LogLimits& limits) override {
    ++calls;
    if(mode==1)return make_unexpected(log_error(LogErrc::BackendFailure));
    if(mode==2)throw std::runtime_error("private factory sentinel");
    if(mode==3)return std::shared_ptr<LogPort>{};
    if(mode==4)return std::shared_ptr<LogPort>(std::shared_ptr<void>{},log.get());
    auto actual=limits;
    if(limits_mismatch==1)actual.record_capacity=limits.record_capacity==1?2:1;
    if(limits_mismatch==2)actual.full=limits.full==LogOverflow::DropOldest?LogOverflow::RejectNewest:LogOverflow::DropOldest;
    if(limits_mismatch==3)actual.minimum_level=limits.minimum_level==LogLevel::Info?LogLevel::Error:LogLevel::Info;
    auto made=observability::make_memory_logging(id,1,actual);CHECK(made);log->inner=made->writer;return log;
  }
};
inline observability::SafeLogger* business_log=nullptr;
Result<int> logged_compute(const int& value,WorkContext& context) {
  business_log->try_write({LogLevel::Info,LogComponent::Host,LogEvent::Ready,{}});
  return native_test::compute(value,context);
}
struct EngineEnv {
  struct InlineExecutor final : ExecutorPort {
    unsigned submissions=0;
    Result<void> submit(std::unique_ptr<ReadyWork>) override {
      ++submissions;return make_unexpected(host_error(HostErrc::UnsupportedCapability));
    }
  };
  Ports ports;
  std::vector<int> events;
  std::unique_ptr<NativeHost> host;
  std::optional<HostSession> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::weak_ptr<ModuleLifecyclePort> module_owner;
  std::shared_ptr<InlineExecutor> executor=std::make_shared<InlineExecutor>();
  EngineEnv(bool reading=false,Result<int>(*handler)(const int&,WorkContext&)=native_test::compute,HostOptions options={},
            std::function<void(registry::Registrar&)> custom={},std::shared_ptr<HostLogFactoryPort> factory={}) {
    auto supplied=ports.value();supplied.logging_factory=std::move(factory);
    auto made=NativeHost::create(options,policy_test::configuration(),supplied);CHECK(made);host=std::move(*made);
    auto lifecycle=std::make_shared<Lifecycle>(events,1);
    module_owner=lifecycle;
    auto m=module("native",lifecycle);
    m.registration.manifest.operations.push_back(key());m.registration.manifest.executors.push_back(name("test"));
    m.registration.executors.push_back({name("test"),name("app"),false,false,executor});
    if(reading) {
      m.registration.manifest.services.push_back(name("reader"));
      m.registration.manifest.required_services.push_back({{name("native"),name("reader")},CppTypeToken::of<Reader>()});
      m.registration.services.push_back(*registry::ServiceBinding::make(name("reader"),std::make_shared<Reader>()));
    }
    m.registration.register_operations=[reading,handler](registry::Registrar& registrar) {
      DefinitionInput d{key(),{},{true,false,false,name("test"),name("app")},reading?AtomicMode::Incompatible:AtomicMode::PureCompute,{name("allow")},"host"};
      registry::OperationOptions o{{},{},{name("native"),name("test")},{},false};
      if(reading){o.read_service=registry::ServiceRef{name("native"),name("reader")};CHECK(registrar.read(native_test::read,d,o));}
      else CHECK(registrar.compute(handler,d,o));
    };
    if(custom)m.registration.register_operations=std::move(custom);
    CHECK(host->add(m));CHECK(host->start());
    auto opened=host->open({{std::byte{7}}},{policy_test::rules(),ports.auth->identity.deadline,false});CHECK(opened);
    session.emplace(std::move(*opened));auto verified=session->verify({policy_test::principal(),{}, {}});CHECK(verified);caller=*verified;
  }
  ~EngineEnv(){if(host)host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));}
  auto bind(){return session->bind<int,int>(key(),{},Shape::Read,caller,std::array{policy_test::target()},native_test::targets,name("host.call"));}
  template<class R> auto bind_as(){return session->bind<int,R>(key(),{},Shape::Read,caller,std::array{policy_test::target()},native_test::targets,name("host.call"));}
  auto options(){return invocation::InvokeOptions{{},ports.clock->now()+std::chrono::minutes(1),100};}
};

void budgets_owners() {
  Ports ports;
  auto options = HostOptions{};
  auto config = policy_test::configuration();
  CHECK(!NativeHost::create(options,config,{}));
  auto invalid = ports.value();
  invalid.authentication = std::shared_ptr<policy::TrustedAuthenticationPort>(std::shared_ptr<void>{},ports.auth.get());
  CHECK(!NativeHost::create(options,config,invalid));
  for (std::size_t n : {std::size_t{0},std::size_t{4097}}) {
    options.host.active_admissions=n;
    CHECK(!NativeHost::create(options,config,ports.value()));
  }
  options=HostOptions{};
  options.host.cleanup_errors=0;
  CHECK(!NativeHost::create(options,config,ports.value()));
  options=HostOptions{};
  options.host.failed_start_cleanup=std::chrono::milliseconds(0);
  CHECK(!NativeHost::create(options,config,ports.value()));
  options=HostOptions{};
  options.native.observation_capacity=(std::numeric_limits<std::size_t>::max)();
  check(!NativeHost::create(options,config,ports.value()),"Native observation byte overflow rejected before create");
  auto first=NativeHost::create(HostOptions{},config,ports.value());
  auto second=NativeHost::create(HostOptions{},config,ports.value());
  CHECK(first&&second);
  CHECK((*first)->incarnation()!=(*second)->incarnation());
  CHECK((*first)->snapshot({}).quiescent);
}

void ready_gate() {
  Ports ports;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());
  CHECK(made);
  auto& host=**made;
  auto attempt=policy::AuthenticationAttempt{{std::byte{7}}};
  auto delegation=policy::DelegationInput{policy_test::rules(),ports.clock->now()+std::chrono::minutes(1),false};
  CHECK(!host.open(attempt,delegation));
  CHECK(host.snapshot({}).phase==HostPhase::Configuring);
  std::array<PendingCleanup,8> pending{};
  auto stopped=host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1),pending);
  CHECK(stopped.quiescent);
  CHECK(stopped.disposition==ShutdownDisposition::Complete);
  CHECK(stopped.pending_total==0&&stopped.pending_written==0&&!stopped.pending_truncated);
  CHECK(!host.open(attempt,delegation));
  CHECK(!host.start());
  CHECK(host.shutdown_until(std::chrono::steady_clock::now()).quiescent);
}
void start_order() {
  Ports ports;std::vector<int> events;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
  auto a=std::make_shared<Lifecycle>(events,1),b=std::make_shared<Lifecycle>(events,2);
  auto& host=**made;
  unsigned inside=0;
  auto observe=[&](const ModuleContext& context){
    CHECK(host.snapshot({}).phase==HostPhase::Starting);
    auto wrote=context.log.try_write({LogLevel::Info,LogComponent::Host,LogEvent::ModuleStarted,{}});
    CHECK(wrote.decision==LogDecision::Accepted&&wrote.accepted);
    auto position=context.log.snapshot();CHECK(position&&position->accepted_through.accepted_sequence>0);
    CHECK(context.log.flush(position->accepted_through));++inside;
  };
  a->on_start=observe;b->on_start=observe;
  CHECK(host.add(module("b",b,"a")));CHECK(host.add(module("a",a)));
  CHECK(host.start());
  auto snap=host.snapshot({});CHECK(snap.phase==HostPhase::Ready);CHECK(snap.started_modules==2);
  auto stopped=host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  CHECK(stopped.quiescent);CHECK(events==std::vector<int>({1,2,-2,-1}));CHECK(inside==2);
  CHECK(!host.start());
}
void start_failures() {
  Ports ports;std::vector<int> events;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
  auto a=std::make_shared<Lifecycle>(events,1),b=std::make_shared<Lifecycle>(events,2);b->fail_start=true;
  auto& host=**made;
  CHECK(host.add(module("a",a)));CHECK(host.add(module("b",b,"a")));
  CHECK(!host.start());CHECK(host.snapshot({}).quiescent);
  CHECK(events==std::vector<int>({1,2,-1}));
  CHECK(host.snapshot({}).phase==HostPhase::Failed);
  auto foreign=std::make_shared<ForeignLogging>();auto p=ports.value();p.logging_factory=foreign;
  auto second=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(second);
  CHECK(!(*second)->start());
  // 身份无效的实际后端仍应被成功关闭，不能永久保留为未排空。
  CHECK((*second)->snapshot({}).quiescent);
  auto closed=foreign->owner->snapshot();CHECK(closed&&closed->state==LogState::Closed);
  for(int mismatch=1;mismatch<=3;++mismatch) {
    auto factory=std::make_shared<ControlledFactory>();factory->limits_mismatch=mismatch;
    factory->log->fail_close=mismatch==3;p.logging_factory=factory;
    auto attempt=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(attempt);
    auto started=(*attempt)->start();
    const bool rejected=!started&&started.error().code()==host_error(HostErrc::LoggingUnavailable).code();
    factory->log->fail_close=false;
    auto stopped=(*attempt)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
    CHECK(rejected&&stopped.quiescent&&factory->log->close_calls==(mismatch==3?2u:1u));
  }
  for(int mode=1;mode<=7;++mode) {
    auto factory=std::make_shared<ControlledFactory>();factory->mode=mode;
    factory->log->fail_snapshot=mode==5||mode==7;factory->log->throw_snapshot=mode==6;
    factory->log->fail_close=mode==7;p.logging_factory=factory;
    auto attempt=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(attempt);CHECK(factory->calls==0);
    CHECK(!(*attempt)->start());auto failed=(*attempt)->snapshot({});
    CHECK(factory->calls==1&&failed.phase==HostPhase::Failed&&failed.primary_error);
    if(mode==7) {
      CHECK(!failed.quiescent&&factory->log->close_calls==1);
      factory->log->fail_close=false;
      auto retry=(*attempt)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
      CHECK(retry.quiescent&&retry.primary_error==failed.primary_error);
    } else CHECK(failed.quiescent);
    if(mode>=5)CHECK(factory->log->close_calls==(mode==7?2u:1u));
  }
  for(int fail_at:{1,2}) {
    std::vector<int> actual;
    auto attempt=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(attempt);
    auto first=std::make_shared<Lifecycle>(actual,1),last=std::make_shared<Lifecycle>(actual,2);
    (fail_at==1?first:last)->on_start=[](const ModuleContext&){throw std::runtime_error("private module sentinel");};
    CHECK((*attempt)->add(module("a",first)));CHECK((*attempt)->add(module("b",last,"a")));
    CHECK(!(*attempt)->start());CHECK((*attempt)->snapshot({}).quiescent);
    CHECK(actual==(fail_at==1?std::vector<int>{1}:std::vector<int>{1,2,-1}));
  }
}
void pending_report() {
  Ports ports;std::vector<int> events;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
  auto a=std::make_shared<Lifecycle>(events,1);a->fail_stop=true;
  auto& host=**made;CHECK(host.add(module("a",a)));CHECK(host.start());
  std::array<PendingCleanup,1> small{};
  auto stopped=host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1),small);
  const bool refused=!stopped.quiescent&&stopped.pending_total==2&&stopped.pending_written==1&&stopped.pending_truncated&&
    small[0].kind==PendingKind::Module&&small[0].module==name("a");
  a->fail_stop=false;
  CHECK(host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(refused);
}
void lifecycle_concurrency();
void lifecycle_reentrancy() {
  Ports ports;auto factory=std::make_shared<ReentrantLogging>();auto p=ports.value();p.logging_factory=factory;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(made);
  factory->host=made->get();CHECK((*made)->start());
  CHECK((*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(factory->refused&&factory->not_ready);
  lifecycle_concurrency();
}
void catalog_registration() {
  Ports ports;std::vector<int> events;auto m=module("a",std::make_shared<Lifecycle>(events,1));
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
  CHECK((*made)->add(m));auto duplicate=(*made)->add(m);(void)duplicate;
  CHECK(!(*made)->start());CHECK(events.empty());
  CHECK((*made)->snapshot({}).primary_error);CHECK((*made)->snapshot({}).quiescent);
  auto caps=(*made)->capabilities();CHECK(caps.native_read&&caps.native_compute&&caps.ordinary_memory_logging);
  CHECK(!caps.async_execution&&!caps.execution_observation&&!caps.state&&!caps.storage&&!caps.restore);
  auto sticky=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(sticky);
  auto invalid=m;invalid.lifecycle.reset();CHECK(!(*sticky)->add(invalid));
  CHECK(!(*sticky)->add(m));CHECK(!(*sticky)->start());CHECK(events.empty());
  for(int invalid_kind=0;invalid_kind<4;++invalid_kind) {
    std::vector<int> starts;
    auto attempt=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(attempt);
    auto bad=module("a",std::make_shared<Lifecycle>(starts,1));
    if(invalid_kind==0)bad.registration.manifest.dependencies.push_back({name("missing"),ver()});
    if(invalid_kind==1)bad.registration.manifest.dependencies.push_back({name("a"),ver()});
    if(invalid_kind==2)bad.registration.manifest.operations.push_back(key());
    if(invalid_kind==3)bad.registration.manifest.required_resources.push_back({name("a"),name("resource")});
    auto registered=(*attempt)->add(bad);(void)registered;
    CHECK(!(*attempt)->start());CHECK(starts.empty()&&(*attempt)->snapshot({}).quiescent);
  }
}
void native_results() {
  for(bool reading:{false,true}) {
    EngineEnv e(reading);auto bound=e.bind();CHECK(bound);native_test::entered=0;
    auto reply=bound->invoke(5,e.options());CHECK(native_test::result(reply)==(reading?5+Reader{}.read():7));
    CHECK(native_test::entered==1);CHECK(std::holds_alternative<Rejected>(bound->invoke(-1,e.options())));CHECK(native_test::entered==1);
    auto options=e.options();options.work_limit=0;CHECK(std::holds_alternative<Rejected>(bound->invoke(2,options)));CHECK(native_test::entered==1);
    CHECK(e.executor->submissions==0);
  }
}
void session_ownership() {
  EngineEnv e;auto bound=e.bind();CHECK(bound);
  EngineEnv other;
  CHECK((!e.session->bind<int,int>(key(),{},Shape::Read,other.caller,std::array{policy_test::target()},native_test::targets,name("host.call"))));
  auto moved_session=std::move(*e.session);CHECK(!e.session->verify({policy_test::principal(),{}, {}}));
  e.session.emplace(std::move(moved_session));
  auto moved_bound=std::move(*bound);CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  *bound=std::move(moved_bound);
  e.session.reset();CHECK(native_test::result(bound->invoke(2,e.options()))==4);
  CHECK(e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  e.host.reset();CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  HostOptions one;one.policy.sessions=1;EngineEnv quota(false,native_test::compute,one);
  auto original=quota.bind();CHECK(original);
  std::optional<HostBound<int,int>> retained{std::move(*original)};
  CHECK(quota.session->close());CHECK(std::holds_alternative<Rejected>(retained->invoke(2,quota.options())));
  quota.session.reset();quota.caller.reset();
  auto reopen=[&]{return quota.host->open({{std::byte{7}}},{policy_test::rules(),quota.ports.auth->identity.deadline,false});};
  CHECK(!reopen());retained.reset();CHECK(reopen());
}
void delegation_revoke() {
  EngineEnv e;auto bound=e.bind();CHECK(bound);
  CHECK(e.session->restrict_delegation({{},e.ports.auth->identity.deadline,false}));
  CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  CHECK(!e.session->restrict_delegation({policy_test::rules(),e.ports.auth->identity.deadline,false}));
  CHECK(e.session->close());
  CHECK(!e.session->verify({policy_test::principal(),{}, {}}));
  EngineEnv expired;auto live=expired.bind();CHECK(live);
  expired.ports.clock->elapsed=3600000;
  CHECK(std::holds_alternative<Rejected>(live->invoke(2,expired.options())));
}
void shutdown_deadline() {
  Ports ports;auto factory=std::make_shared<ControlledFactory>();auto p=ports.value();p.logging_factory=factory;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(made);CHECK((*made)->start());
  auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(15);
  factory->log->delay_until=deadline+std::chrono::milliseconds(5);
  auto report=(*made)->shutdown_until(deadline);
  bool refused=!report.quiescent&&report.deadline_exceeded&&factory->log->close_calls==0&&report.pending_total==1;
  CHECK((*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(refused);CHECK(factory->log->close_calls==1);
  auto expired_factory=std::make_shared<ControlledFactory>();p.logging_factory=expired_factory;
  auto expired_host=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(expired_host);CHECK((*expired_host)->start());
  auto writes=expired_factory->log->write_calls;
  auto past=(*expired_host)->shutdown_until(std::chrono::steady_clock::now()-std::chrono::seconds(1));
  bool no_callback=expired_factory->log->write_calls==writes&&expired_factory->log->close_calls==0;
  CHECK((*expired_host)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(past.deadline_exceeded&&!past.quiescent);
  check(no_callback,"expired shutdown must not call logging backend");
  auto final_factory=std::make_shared<ControlledFactory>();p.logging_factory=final_factory;
  auto final_host=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(final_host);CHECK((*final_host)->start());
  auto final_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(15);
  final_factory->log->close_delay=final_deadline+std::chrono::milliseconds(5);
  auto final_report=(*final_host)->shutdown_until(final_deadline);
  CHECK(final_report.quiescent&&final_report.deadline_exceeded&&final_report.pending_total==0);
}
void shutdown_errors() {
  Ports ports;auto factory=std::make_shared<ControlledFactory>();auto p=ports.value();p.logging_factory=factory;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(made);CHECK((*made)->start());
  factory->log->fail_close=true;
  auto report=(*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  bool refused=!report.quiescent&&report.cleanup_error_count==1&&report.pending_total==1&&factory->log->close_calls==1;
  factory->log->fail_close=false;
  auto complete=(*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  CHECK(complete.quiescent);CHECK(complete.disposition==ShutdownDisposition::CompleteWithErrors);CHECK(refused);
  auto throwing_factory=std::make_shared<ControlledFactory>();p.logging_factory=throwing_factory;
  auto throwing=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(throwing);CHECK((*throwing)->start());
  throwing_factory->log->throw_close=true;
  auto caught=(*throwing)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  throwing_factory->log->throw_close=false;
  auto retried=(*throwing)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  CHECK(!caught.quiescent&&caught.cleanup_error_count==1&&retried.quiescent&&throwing_factory->log->close_calls==2);
  HostOptions limited;limited.host.cleanup_errors=1;std::vector<int> events;
  auto guarded=NativeHost::create(limited,policy_test::configuration(),ports.value());CHECK(guarded);
  auto a=std::make_shared<Lifecycle>(events,1),b=std::make_shared<Lifecycle>(events,2);unsigned attempts=0;
  a->on_stop=[] {return ModuleStopResult{true,host_error(HostErrc::InvalidInput)};};
  b->on_stop=[&]() -> ModuleStopResult {
    if(++attempts==1)throw std::runtime_error("private stop sentinel");
    return {true,host_error(HostErrc::InvalidInput)};
  };
  CHECK((*guarded)->add(module("a",a)));CHECK((*guarded)->add(module("b",b,"a")));CHECK((*guarded)->start());
  std::array<PendingCleanup,4> pending{};
  auto failed=(*guarded)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1),pending);
  CHECK(!failed.quiescent&&failed.pending_total==3&&pending[0].module==name("b")&&pending[1].module==name("a"));
  auto recovered=(*guarded)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  CHECK(recovered.quiescent&&recovered.cleanup_error_count==3&&!recovered.primary_error);
  std::array<CleanupError,1> errors{};auto snapshot=(*guarded)->snapshot(errors);
  CHECK(snapshot.cleanup_written==1&&snapshot.cleanup_truncated&&snapshot.cleanup_error_count==3);
  CHECK(errors[0].module==name("b")&&errors[0].code==host_error(HostErrc::CallbackException).code());
  CHECK(events==std::vector<int>({1,2,-2,-2,-1}));
}
void logging_ownership() {
  EngineEnv e;
  std::array<PublicLogRecord,16> records{};
  auto page=e.host->copy_logs({e.host->incarnation(),1,0},records);CHECK(page&&page->copied>=4);
  for(std::size_t i=0;i<page->copied;++i)CHECK(records[i].position.host==e.host->incarnation());
  auto before=page->accepted_upper;
  auto bound=e.bind();CHECK(bound);CHECK(native_test::result(bound->invoke(2,e.options()))==4);
  auto after=e.host->copy_logs(before,records);CHECK(after&&after->copied==0);
  CHECK(e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  auto stopped=e.host->copy_logs(before,records);CHECK(stopped&&stopped->copied);
  const auto& last=records[stopped->copied-1];
  CHECK(std::string_view(last.text.data(),last.text_size).find("log_closing")!=std::string_view::npos);
  auto factory=std::make_shared<ControlledFactory>();EngineEnv failing(false,logged_compute,{}, {},factory);
  factory->log->throw_write=true;
  auto logger=observability::SafeLogger::create(factory->log);CHECK(logger);
  business_log=&*logger;
  auto real=failing.bind();CHECK(real);auto actual=real->invoke(2,failing.options());business_log=nullptr;
  CHECK(native_test::result(actual)==4);
  auto counters=logger->counters();CHECK(counters&&counters->write_backend_exceptions==1);
  auto unsupported=failing.host->copy_logs({failing.host->incarnation(),1,0},records);
  CHECK(!unsupported&&unsupported.error().code()==host_error(HostErrc::UnsupportedCapability).code());
  CHECK(failing.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
}
struct CallGate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,release=false;
};
struct WaitingFactory final : HostLogFactoryPort {
  CallGate gate;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation id,const LogLimits& limits) override {
    {std::unique_lock lock(gate.mutex);gate.entered=true;gate.changed.notify_all();
      if(!gate.changed.wait_for(lock,std::chrono::seconds(2),[&]{return gate.release;}))
        return make_unexpected(log_error(LogErrc::BackendFailure));}
    auto made=observability::make_memory_logging(id,1,limits);CHECK(made);return made->writer;
  }
};
void lifecycle_concurrency() {
  Ports ports;auto factory=std::make_shared<WaitingFactory>();auto p=ports.value();p.logging_factory=factory;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),p);CHECK(made);
  bool started=false;std::thread worker([&]{started=bool((*made)->start());});
  bool reached=false;
  {std::unique_lock lock(factory->gate.mutex);reached=factory->gate.changed.wait_for(lock,std::chrono::seconds(1),[&]{return factory->gate.entered;});}
  auto competing_start=(*made)->start();
  auto competing_stop=(*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  auto observed=(*made)->snapshot({});
  {std::lock_guard lock(factory->gate.mutex);factory->gate.release=true;}factory->gate.changed.notify_all();worker.join();
  CHECK((*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(reached&&started&&!competing_start&&competing_start.error().code()==host_error(HostErrc::Busy).code());
  CHECK(competing_stop.disposition==ShutdownDisposition::Busy&&!competing_stop.quiescent&&!observed.quiescent);
  std::vector<int> events;auto own=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(own);
  auto other=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(other);
  auto life=std::make_shared<Lifecycle>(events,1);bool reentrant=false,cross=false;
  life->on_start=[&](const ModuleContext&){
    auto report=(*own)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
    reentrant=report.disposition==ShutdownDisposition::Reentrant&&(*own)->snapshot({}).phase==HostPhase::Starting;
    cross=(*other)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent;
  };
  CHECK((*own)->add(module("a",life)));CHECK((*own)->start());
  CHECK((*own)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);CHECK(reentrant&&cross);
}
inline CallGate* call_gate=nullptr;
Result<int> held_compute(const int& value,WorkContext&) {
  std::unique_lock lock(call_gate->mutex);call_gate->entered=true;call_gate->changed.notify_all();
  if(!call_gate->changed.wait_for(lock,std::chrono::seconds(2),[]{return call_gate->release;}))
    return make_unexpected(host_error(HostErrc::DeadlineExceeded));
  return value+2;
}
void admission_race() {
  CallGate gate;call_gate=&gate;
  HostOptions options;options.host.active_admissions=1;
  EngineEnv e(false,held_compute,options);e.ports.threads->any_thread=true;auto bound=e.bind();CHECK(bound);
  bool actual_result=false;
  std::thread worker([&]{try{actual_result=native_test::result(bound->invoke(2,e.options()))==4;}catch(...){actual_result=false;}});
  bool saw=false;
  {std::unique_lock lock(gate.mutex);saw=gate.changed.wait_for(lock,std::chrono::seconds(1),[&]{return gate.entered;});}
  auto blocked=bound->invoke(3,e.options());
  std::array<PendingCleanup,8> pending{};
  auto stopped=e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::milliseconds(15),pending);
  auto late=bound->invoke(2,e.options());
  {std::lock_guard lock(gate.mutex);gate.release=true;}gate.changed.notify_all();worker.join();
  CHECK(e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(saw&&actual_result);CHECK(std::holds_alternative<Rejected>(blocked));
  CHECK(!stopped.quiescent&&stopped.deadline_exceeded&&stopped.active_admissions==1);
  CHECK(pending[0].kind==PendingKind::Admissions&&pending[0].active_admissions==1);
  CHECK(std::holds_alternative<Rejected>(late));
}
inline std::function<void()> result_callback;
inline NativeHost* return_host=nullptr;
inline unsigned moves_observed=0;
inline bool moves_admitted=true;
Result<HostMoveValue> move_result(const int& value,WorkContext&) {
  if(result_callback)result_callback();return HostMoveValue{value};
}
Result<void> void_result(const int&,WorkContext&) {return {};}
template<class Function> auto register_compute(Function function) {
  return [function](registry::Registrar& registrar) {
    DefinitionInput d{key(),{},{true,false,false,name("test"),name("app")},AtomicMode::PureCompute,{name("allow")},"host return"};
    registry::OperationOptions o{{},{},{name("native"),name("test")},{},false};
    CHECK(registrar.compute(function,d,o));
  };
}
void return_lifetime() {
  EngineEnv e(false,native_test::compute,{},register_compute(move_result));
  auto made=e.bind_as<HostMoveValue>();CHECK(made);
  std::optional<HostBound<int,HostMoveValue>> bound{std::move(*made)};
  result_callback=[&]{bound.reset();e.session.reset();};return_host=e.host.get();moves_observed=0;moves_admitted=true;
  HostMoveValue::moving=[]() noexcept {++moves_observed;moves_admitted=moves_admitted&&return_host->snapshot({}).active_admissions>0;};
  auto* outer=&*bound;auto reply=outer->invoke(3,e.options());HostMoveValue::moving=nullptr;result_callback={};
  CHECK(!bound&&!e.session);CHECK(moves_observed&&moves_admitted);CHECK(std::holds_alternative<Completed<HostMoveValue>>(reply));
  auto& moved_outcome=std::get<Completed<HostMoveValue>>(reply).outcome.value();
  CHECK(std::holds_alternative<ReadCompleted<HostMoveValue>>(moved_outcome));
  auto& actual=std::get<ReadCompleted<HostMoveValue>>(moved_outcome).result;CHECK(actual&&actual->value==3);
  CHECK(e.host->snapshot({}).active_admissions==0);
  EngineEnv v(false,native_test::compute,{},register_compute(void_result));auto vb=v.bind_as<void>();CHECK(vb);
  auto void_reply=vb->invoke(1,v.options());CHECK(std::holds_alternative<Completed<void>>(void_reply));
  auto& void_outcome=std::get<Completed<void>>(void_reply).outcome.value();
  CHECK(std::holds_alternative<ReadCompleted<void>>(void_outcome)&&std::get<ReadCompleted<void>>(void_outcome).result);
}
void shutdown_order() {
  Ports ports;std::vector<int> events;
  auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
  auto a=std::make_shared<Lifecycle>(events,1),b=std::make_shared<Lifecycle>(events,2);bool phase_ok=false;
  b->on_stop=[&]{phase_ok=(*made)->snapshot({}).phase==HostPhase::StopModulesObservers;
    return ModuleStopResult{true,host_error(HostErrc::CallbackException)};};
  CHECK((*made)->add(module("b",b,"a")));CHECK((*made)->add(module("a",a)));CHECK((*made)->start());
  auto report=(*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
  CHECK(report.quiescent&&report.disposition==ShutdownDisposition::CompleteWithErrors);
  CHECK(phase_ok&&events==std::vector<int>({1,2,-2,-1}));
  CHECK(report.cleanup_error_count==1);
}
void steady_allocation() {
  namespace probe=native_test::allocation;
  probe::initialize();EngineEnv e;auto bound=e.bind();CHECK(bound);auto options=e.options();
  for(unsigned i=0;i<4;++i)CHECK(native_test::result(bound->invoke(2,options))==4);
  std::array<probe::Counts,40> counts{};std::array<bool,40> success{};
  for(std::size_t i=0;i<counts.size();++i) {
    probe::start();
    {auto reply=bound->invoke(2,options);
      if(auto* completed=std::get_if<Completed<int>>(&reply))
        if(auto* read=std::get_if<ReadCompleted<int>>(&completed->outcome.value()))success[i]=bool(read->result)&&*read->result==4;
    }
    counts[i]=probe::stop();
  }
  bool valid=true;
  std::cout<<"{\"format\":\"ock.host-allocation/1\",\"case\":\"T23.host.steady_allocation\","
    "\"modules\":[\"Host\",\"Invocation\",\"Policy\",\"Registry\",\"Logging\",\"CoreContracts\",\"Foundation\"],"
    "\"ordinary_log_per_invoke\":false,\"warmup\":4,\"samples\":[";
  for(std::size_t i=0;i<counts.size();++i) {
    auto c=counts[i];valid=valid&&success[i]&&native_test::zero(c);
    if(i)std::cout<<',';
    std::cout<<"{\"index\":"<<i<<",\"success\":"<<(success[i]?"true":"false")<<",\"cpp\":"<<c.cpp<<",\"crt\":";
    if(probe::crt_available())std::cout<<c.crt;else std::cout<<"null";
    std::cout<<",\"asan\":";if(probe::asan_available())std::cout<<c.asan_allocations;else std::cout<<"null";
    std::cout<<'}';
  }
  std::cout<<"],\"verified\":"<<(valid?"true":"false")<<"}\n";
  CHECK(valid);
}
void owner_reclamation() {
  EngineEnv e;auto result=e.bind();CHECK(result);
  std::optional<HostBound<int,int>> bound{std::move(*result)};auto weak=e.module_owner;
  CHECK(e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  CHECK(!weak.expired());e.session.reset();e.host.reset();CHECK(!weak.expired());
  bound.reset();CHECK(weak.expired());
}
int destructor_probe(std::string_view mode) {
  if(mode=="--host-destructor-safe") {
    {EngineEnv e;CHECK(e.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);}
    std::cerr<<"host_destructor_safe\n";return 0;
  }
  if(mode=="--host-destructor-ready") {
    EngineEnv e;std::cerr<<"host_destructor_probe ready\n";e.host.reset();return 91;
  }
  if(mode=="--host-destructor-active") {
    EngineEnv e(false,native_test::compute,{},register_compute(move_result));auto bound=e.bind_as<HostMoveValue>();CHECK(bound);
    result_callback=[&]{std::cerr<<"host_destructor_probe active\n";e.host.reset();};
    bound->invoke(1,e.options());return 91;
  }
  if(mode=="--host-destructor-failed") {
    Ports ports;std::vector<int> events;auto made=NativeHost::create(HostOptions{},policy_test::configuration(),ports.value());CHECK(made);
    auto a=std::make_shared<Lifecycle>(events,1),b=std::make_shared<Lifecycle>(events,2);a->fail_stop=true;b->fail_start=true;
    CHECK((*made)->add(module("a",a)));CHECK((*made)->add(module("b",b,"a")));CHECK(!(*made)->start());
    std::cerr<<"host_destructor_probe failed\n";made->reset();return 91;
  }
  return 2;
}
}

int main(int argc,char** argv) {
  _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
  const std::map<std::string,void(*)()> cases{
    {"T03.host.configuration",host_test::budgets_owners},
    {"T03.host.ready_gate",host_test::ready_gate},
    {"T03.host.start_order",host_test::start_order},
    {"T03.host.start_failures",host_test::start_failures},
    {"T22.host.pending_report",host_test::pending_report},
    {"T22.host.lifecycle_reentrancy",host_test::lifecycle_reentrancy},
    {"T03.host.catalog_registration",host_test::catalog_registration},
    {"T03.host.native_results",host_test::native_results},
    {"T03.host.session_ownership",host_test::session_ownership},
    {"T03.host.delegation_revoke",host_test::delegation_revoke},
    {"T22.host.shutdown_deadline",host_test::shutdown_deadline},
    {"T22.host.shutdown_errors",host_test::shutdown_errors},
    {"T19.host.logging_ownership",host_test::logging_ownership},
    {"T22.host.admission_race",host_test::admission_race},
    {"T22.host.return_lifetime",host_test::return_lifetime},
    {"T22.host.shutdown_order",host_test::shutdown_order},
    {"T22.host.destructor_guard",[]{CHECK(host_test::destructor_probe("--host-destructor-safe")==0);}},
    {"T23.host.allocation_controls",native_test::allocation_probe},
    {"T23.host.steady_allocation",host_test::steady_allocation},
    {"T23.host.owner_reclamation",host_test::owner_reclamation}
  };
  try {
    if(argc==2&&std::string_view(argv[1]).starts_with("--host-destructor-"))return host_test::destructor_probe(argv[1]);
    if(argc==2&&std::string_view(argv[1])=="--asan-hook-registration-failure")return native_test::allocation_registration_failure();
    if(argc==2&&std::string_view(argv[1])=="--allocation-injected-red")return native_test::allocation_injected_red();
    if(argc==2&&std::string_view(argv[1])=="--list") {
      for(auto& [name,run]:cases) std::cout<<name<<'\n';
      return 0;
    }
    if(argc!=2) return 2;
    auto found=cases.find(argv[1]);
    if(found==cases.end()) return 2;
    found->second();
    return 0;
  } catch(const std::exception& error) {
    native_test::allocation::stop();
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
