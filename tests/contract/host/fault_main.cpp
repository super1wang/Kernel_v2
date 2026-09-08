#include <ock/runtime/host.hpp>
#include "fault_allocations.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <crtdbg.h>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
namespace {
using namespace ock::contracts;
using namespace ock::runtime;
using namespace host;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
#define REQUIRE(...) require(bool((__VA_ARGS__)),#__VA_ARGS__)
template<class T> T identity(){T value{};value.bytes[0]=1;return value;}
struct Clock final:policy::ClockPort {
  mutable unsigned calls=0;
  policy::TimePoint now() const noexcept override{++calls;return std::chrono::steady_clock::now();}
};
struct Auth final:policy::TrustedAuthenticationPort {
  unsigned calls=0;
  Result<policy::AuthenticatedIdentity> authenticate(const policy::AuthenticationAttempt&) override {
    ++calls;return make_unexpected(policy::policy_error(policy::PolicyErrc::AuthenticationFailed));
  }
};
struct Digest final:policy::TrustedGroupDigestPort {
  unsigned calls=0;
  Result<ContractDigest> fingerprint(const policy::GroupSnapshot&) override{++calls;return ContractDigest{};}
};
struct Threads final:invocation::TrustedThreadPort {
  mutable unsigned calls=0;
  Result<invocation::ThreadObservation> current() const noexcept override {
    ++calls;return make_unexpected(invocation::invocation_error(invocation::InvocationErrc::ThreadRejected));
  }
};
struct Ports {
  std::shared_ptr<Clock> clock=std::make_shared<Clock>();
  std::shared_ptr<Auth> auth=std::make_shared<Auth>();
  std::shared_ptr<Digest> digest=std::make_shared<Digest>();
  std::shared_ptr<Threads> threads=std::make_shared<Threads>();
  HostPorts value() const{return {auth,clock,digest,threads,{}};}
  void no_callbacks() const{REQUIRE(!clock->calls&&!auth->calls&&!digest->calls&&!threads->calls);}
};
struct Owner final:PortLifetime{};
policy::PolicyConfiguration configuration(std::shared_ptr<PortLifetime> owner={}) {
  policy::PolicyConfiguration c;
  c.principals.push_back({{identity<PrincipalId>()},{}});
  if(owner)c.targets.push_back({identity<ock::foundation::ObjectId>(),1,{},std::move(owner)});
  return c;
}
void sample(const char* phase,const host_fault::AllocationObservation& observation) {
  std::printf("{\"phase\":\"%s\",\"cpp_new_calls\":%zu,\"failed_ordinal\":%zu,\"failed_size\":%zu,\"live_cpp_blocks\":%lld}\n",
    phase,observation.calls,observation.failed_ordinal,observation.failed_size,static_cast<long long>(observation.live_blocks));
}
void allocator_controls() {
  const auto before=host_fault::live_blocks();
  host_fault::begin_allocations(1);
  bool caught=false;try{void* p=::operator new(19);::operator delete(p);}catch(const std::bad_alloc&){caught=true;}
  void* second=::operator new(23);::operator delete(second);
  auto observation=host_fault::end_allocations();
  REQUIRE(caught&&observation.failed_ordinal==1&&observation.failed_size==19&&observation.calls==2);
  REQUIRE(host_fault::live_blocks()==before);
  host_fault::begin_allocations(1);
  auto* p=::operator new[](41,std::align_val_t(64),std::nothrow);
  observation=host_fault::end_allocations();
  REQUIRE(!p&&observation.failed_ordinal==1&&host_fault::live_blocks()==before);
  host_fault::begin_allocations();
  p=::operator new[](41,std::align_val_t(64),std::nothrow);
  REQUIRE(p);::operator delete[](p,std::align_val_t(64));
  observation=host_fault::end_allocations();
  REQUIRE(observation.calls==1&&!observation.failed_ordinal&&host_fault::live_blocks()==before);
  sample("allocator_positive_negative_and_self_disarm",observation);
}
void stl_default_vector() {
  host_fault::begin_allocations(1);
  std::vector<int> value;
  const auto observed=host_fault::end_allocations();
#if _ITERATOR_DEBUG_LEVEL == 0
  REQUIRE(observed.calls==0&&!observed.failed_ordinal&&value.empty());
  sample("stl_default_vector_no_proxy",observed);
#else
  throw std::runtime_error("expected Debug STL default vector noexcept termination was not observed");
#endif
}
void random_failures() {
  Ports ports;auto config=configuration();auto supplied=ports.value();
  for(auto mode:{host_fault::RandomMode::Real,host_fault::RandomMode::Failure,host_fault::RandomMode::Zero}) {
    host_fault::random_mode(mode);
    const auto before=host_fault::live_blocks();
    {
      auto made=NativeHost::create({},config,supplied);
      if(mode==host_fault::RandomMode::Real)REQUIRE(made);
      else REQUIRE(!made&&made.error().code()==host_error(HostErrc::IdentityUnavailable).code());
    }
    REQUIRE(host_fault::random_calls()==1&&host_fault::random_arguments_valid());
    REQUIRE(host_fault::live_blocks()==before);ports.no_callbacks();
  }
  host_fault::random_mode(host_fault::RandomMode::Real);
  std::puts("random: real BCrypt positive; failed status and all-zero success rejected without callbacks/leaks");
}
void create_windows(std::size_t selected_ordinal=0) {
  Ports ports;auto lifetime=std::make_shared<Owner>();auto config=configuration(lifetime);auto supplied=ports.value();
  // 一次有限成功路径给出该输入实际分配次数，随后每个ordinal均独立注入。
  const auto before=host_fault::live_blocks();
  host_fault::begin_allocations();
  auto made=NativeHost::create({},config,supplied);
  auto baseline=host_fault::end_allocations();REQUIRE(made);
  made->reset();REQUIRE(host_fault::live_blocks()==before);
  REQUIRE(baseline.calls>0&&baseline.calls<=128);sample("create_baseline",baseline);
  const auto owners=lifetime.use_count();
  REQUIRE(!selected_ordinal||selected_ordinal<=baseline.calls);
  for(std::size_t ordinal=selected_ordinal?selected_ordinal:1;
      ordinal<=(selected_ordinal?selected_ordinal:baseline.calls);++ordinal) {
    host_fault::begin_allocations(ordinal);
    auto failed=NativeHost::create({},config,supplied);
    const auto observed=host_fault::end_allocations();
    REQUIRE(!failed&&observed.failed_ordinal==ordinal);
    REQUIRE(failed.error().code()==host_error(HostErrc::BudgetExceeded).code()||
      failed.error().code()==registry::registry_error(registry::RegistryErrc::BudgetExceeded).code());
    REQUIRE(host_fault::live_blocks()==before&&lifetime.use_count()==owners);
    REQUIRE(ports.auth.use_count()==2&&ports.clock.use_count()==2&&ports.digest.use_count()==2&&ports.threads.use_count()==2);
    sample("create_injected",observed);
  }
  ports.no_callbacks();
  auto invalid=HostOptions{};invalid.native.observation_capacity=(std::numeric_limits<std::size_t>::max)();
  host_fault::random_mode(host_fault::RandomMode::Real);host_fault::begin_allocations();
  auto rejected=NativeHost::create(invalid,config,supplied);auto invalid_sample=host_fault::end_allocations();
  REQUIRE(!rejected&&!invalid_sample.calls&&!host_fault::random_calls());
  // 调用者清空配置后，Host仍深拥有target owner；最终Host析构释放该拥有。
  made=NativeHost::create({},config,supplied);REQUIRE(made);
  std::weak_ptr<Owner> weak=lifetime;lifetime.reset();config.targets.clear();
  REQUIRE(!weak.expired());made->reset();REQUIRE(weak.expired());
}
enum class FailWindow { None, Facade, Policy };
struct Backend final:LogPort {
  LogSnapshot state{};
  FailWindow window=FailWindow::None;
  unsigned snapshots=0,closes=0,writes=0;
  bool refuse_close=false,throw_close=false;
  std::array<LogEvent,16> events{};
  void initialize(HostIncarnation id,const LogLimits& limits) {state={{id,1,0},0,0,limits,LogState::Open,{}};}
  LogWriteResult try_write(const LogInput& input) override {
    if(writes>=events.size())return {LogDecision::Rejected,LogReason::BackendFailure,{}};
    events[writes++]=input.event;++state.accepted_through.accepted_sequence;++state.retained_count;++state.counters.accepted;
    return {LogDecision::Accepted,LogReason::None,state.accepted_through};
  }
  Result<LogSnapshot> snapshot() override {
    ++snapshots;
    if(window==FailWindow::Policy&&snapshots==2)host_fault::begin_allocations(1);
    return state;
  }
  Result<LogFlushResult> flush(LogPosition through) override {return LogFlushResult{through,0,true};}
  Result<LogFlushResult> close() override {
    ++closes;if(throw_close)throw std::runtime_error("raw backend close fault");
    if(refuse_close)return make_unexpected(log_error(LogErrc::BackendFailure));
    state.state=LogState::Closed;return LogFlushResult{state.accepted_through,0,true};
  }
};
struct Factory final:HostLogFactoryPort {
  std::shared_ptr<Backend> backend=std::make_shared<Backend>();
  unsigned calls=0;
  Result<std::shared_ptr<LogPort>> create(HostIncarnation id,const LogLimits& limits) override {
    ++calls;backend->initialize(id,limits);
    if(backend->window==FailWindow::Facade)host_fault::begin_allocations(1);
    return backend;
  }
};
struct Lifecycle final:ModuleLifecyclePort {
  unsigned starts=0,stops=0;
  Result<void> start(const ModuleContext&) override{++starts;return {};}
  ModuleStopResult stop() override{++stops;return {true,{}};}
};
void start_window(FailWindow window,int close_failure) {
  Ports ports;auto config=configuration();auto factory=std::make_shared<Factory>();
  factory->backend->window=window;factory->backend->refuse_close=close_failure==1;
  factory->backend->throw_close=close_failure==2;
  auto lifecycle=std::make_shared<Lifecycle>();auto supplied=ports.value();supplied.logging_factory=factory;
  auto made=NativeHost::create({},config,supplied);REQUIRE(made);
  registry::ModuleInput input{registry::ModuleManifest{*Name::parse("fault.module"),*OperationVersion::parse("1.0.0",128)}};
  input.register_operations=[](registry::Registrar&){};
  REQUIRE((*made)->add({input,lifecycle}));
  auto result=(*made)->start();auto observed=host_fault::end_allocations();
  // 无故障正控制正常stop；若故障未命中，也先真实清理再报告反例失败。
  if(result)REQUIRE((*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1)).quiescent);
  REQUIRE(factory->calls==1);
  if(window==FailWindow::None) {
    REQUIRE(result&&lifecycle->starts==1&&lifecycle->stops==1&&factory->backend->closes==1);
  } else {
    REQUIRE(!result&&observed.failed_ordinal==1&&observed.failed_size>0);
    REQUIRE(result.error().code()==(window==FailWindow::Facade?log_error(LogErrc::AllocationFailure):policy::policy_error(policy::PolicyErrc::BudgetExceeded)).code());
    REQUIRE(!lifecycle->starts&&!lifecycle->stops&&factory->backend->closes==1);
    REQUIRE(factory->backend->snapshots==(window==FailWindow::Facade?1u:2u));
    if(window==FailWindow::Policy)REQUIRE(factory->backend->writes>=2&&factory->backend->events[0]==LogEvent::Configured&&factory->backend->events[1]==LogEvent::Starting);
    auto snapshot=(*made)->snapshot({});REQUIRE(snapshot.phase==HostPhase::Failed);
    if(close_failure) {
      REQUIRE(!snapshot.quiescent&&snapshot.cleanup_error_count==1);
      factory->backend->refuse_close=false;
      factory->backend->throw_close=false;
      auto retry=(*made)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(1));
      REQUIRE(retry.quiescent&&factory->backend->closes==2);
    } else REQUIRE(snapshot.quiescent);
  }
  sample(window==FailWindow::Facade?"facade_state":window==FailWindow::Policy?"policy_assembly":"start_positive",observed);
}
}
int main(int argc,char** argv) {
  _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
  _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
  std::set_terminate([] {
    const auto observed=host_fault::end_allocations();
    std::fprintf(stderr,"host_fault_terminate ordinal=%zu size=%zu calls=%zu\n",observed.failed_ordinal,observed.failed_size,observed.calls);
    std::fflush(stderr);std::_Exit(86);
  });
  try {
    if(argc!=2)throw std::runtime_error("one fixed child mode required");
    if(!std::strcmp(argv[1],"create")){allocator_controls();random_failures();create_windows();}
    else if(!std::strcmp(argv[1],"create-full")){allocator_controls();random_failures();create_windows();}
    else if(!std::strcmp(argv[1],"create-recoverable")){allocator_controls();random_failures();create_windows(1);}
    else if(!std::strcmp(argv[1],"create-ordinal-2"))create_windows(2);
    else if(!std::strcmp(argv[1],"stl-default-vector"))stl_default_vector();
    else if(!std::strcmp(argv[1],"start")){start_window(FailWindow::None,0);start_window(FailWindow::Facade,0);start_window(FailWindow::Facade,1);start_window(FailWindow::Facade,2);start_window(FailWindow::Policy,0);}
    else if(!std::strcmp(argv[1],"internal"))host_fault::internal_controls();
    else throw std::runtime_error("unknown child mode");
    std::puts("host_fault_controls_checked");return 0;
  } catch(const std::exception& e) {
    host_fault::end_allocations();std::fprintf(stderr,"host_fault_failure: %s\n",e.what());return 1;
  }
}
