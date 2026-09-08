// 仅此测试翻译单元替换随机调用。先读真实Windows声明，避免重写dllimport声明。
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include "fault_allocations.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
NTSTATUS WINAPI ock_fault_BCryptGenRandom(BCRYPT_ALG_HANDLE,PUCHAR,ULONG,ULONG);
#define BCryptGenRandom ock_fault_BCryptGenRandom
#include "packages/runtime/host/host.cpp"
#undef BCryptGenRandom
#include "tests/contract/authorization/fixtures.hpp"
namespace {
host_fault::RandomMode mode=host_fault::RandomMode::Real;
unsigned calls=0;
bool valid=true;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
}
NTSTATUS WINAPI ock_fault_BCryptGenRandom(BCRYPT_ALG_HANDLE algorithm,PUCHAR bytes,ULONG count,ULONG flags) {
  ++calls;valid=valid&&!algorithm&&bytes&&count==16&&flags==BCRYPT_USE_SYSTEM_PREFERRED_RNG;
  if(mode==host_fault::RandomMode::Failure)return static_cast<NTSTATUS>(0xC0000001L);
  if(mode==host_fault::RandomMode::Zero){std::memset(bytes,0,count);return 0;}
  return BCryptGenRandom(algorithm,bytes,count,flags);
}
namespace host_fault {
void random_mode(RandomMode next) noexcept{mode=next;calls=0;valid=true;}
unsigned random_calls() noexcept{return calls;}
bool random_arguments_valid() noexcept{return valid;}
void internal_controls() {
  using namespace ock::runtime;
  using namespace host;
  ock::contracts::HostIncarnation id{};id.bytes[0]=1;
  NoExecutions source(id);
  require(source.identity()==policy::ObservationSourceIdentity{id,policy::RestoreMode::Absent},"NoExecutions identity");
  auto found=source.find({});auto scan=source.scan({});
  require(!found&&found.error().code()==host_error(HostErrc::UnsupportedCapability).code(),"NoExecutions find");
  require(!scan&&scan.error().code()==host_error(HostErrc::UnsupportedCapability).code(),"NoExecutions scan");
  host::detail::HostControl state({}, {}, {}, id);
  state.options.host.cleanup_errors=1;state.errors.reserve(1);
  state.cleanup_count=(std::numeric_limits<std::uint64_t>::max)()-1;
  const auto error=host_error(HostErrc::CallbackException);
  state.cleanup_error(HostPhase::Finalize,{},error);
  require(state.cleanup_count==(std::numeric_limits<std::uint64_t>::max)()&&!state.cleanup_saturated,"cleanup max-1 increments");
  state.cleanup_error(HostPhase::Finalize,{},error);
  require(state.cleanup_count==(std::numeric_limits<std::uint64_t>::max)()&&state.cleanup_saturated&&state.errors.size()==1,"cleanup max saturates and retention bounded");
  std::puts("internal NoExecutions identity/find/scan and real cleanup_error max-1/max checked");
  // 内部顺序控制直接安装真实PolicyAssembly；它不是公开Host启动流程的替身。
  policy_test::Env policy;
  struct StopObserver final:ModuleLifecyclePort {
    std::shared_ptr<ock::runtime::policy::SessionAuthority> session;
    bool observed=false;
    Result<void> start(const ModuleContext&) override{return {};}
    ModuleStopResult stop() override {
      auto verified=session->verify({policy_test::principal(),{}, {}});
      observed=!verified&&verified.error().code()==ock::runtime::policy::policy_error(ock::runtime::policy::PolicyErrc::StoreClosed).code();
      return {true,{}};
    }
  };
  auto observer=std::make_shared<StopObserver>();observer->session=policy.session;
  host::detail::HostControl shutdown({}, {}, {}, id);
  shutdown.policy=std::move(policy.assembly);shutdown.policy_pending=true;
  shutdown.modules.push_back({name("internal.stop"),observer});shutdown.pending_modules.push_back(0);
  auto report=shutdown.finish(std::chrono::steady_clock::now()+std::chrono::seconds(1),{},false);
  require(report.quiescent&&observer->observed,"real finish closes Policy before module stop; existing session StoreClosed");
  std::puts("internal real HostControl::finish: Policy close_store before module stop verified via existing SessionAuthority StoreClosed");
}
}
