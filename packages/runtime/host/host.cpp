#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <ock/runtime/host.hpp>
#include "packages/runtime/policy/validation.hpp"
#include "packages/runtime/invocation/validation.hpp"

namespace ock::runtime::host {
namespace {
template<class T> bool owned(const std::shared_ptr<T>& p) noexcept { return p&&p.use_count()!=0; }
template<class T> Result<T> failure(HostErrc code) { return make_unexpected(host_error(code)); }
class NoExecutions final : public policy::ExecutionAccessSourcePort {
  HostIncarnation host_;
public:
  explicit NoExecutions(HostIncarnation h):host_(h) {}
  policy::ObservationSourceIdentity identity() const noexcept override { return {host_,policy::RestoreMode::Absent}; }
  Result<policy::ExecutionAccessInput> find(ExecutionRef) override { return failure<policy::ExecutionAccessInput>(HostErrc::UnsupportedCapability); }
  Result<policy::AccessScanPage> scan(const policy::AccessScanRequest&) override { return failure<policy::AccessScanPage>(HostErrc::UnsupportedCapability); }
};
bool budgets(const HostOptions& o) noexcept {
  const auto& h=o.host;const auto& r=o.registration;
  if(!h.active_admissions||h.active_admissions>4096||!h.cleanup_errors||h.cleanup_errors>4096||
     h.failed_start_cleanup.count()<1||h.failed_start_cleanup.count()>60000||
     !r.modules||!r.operations||!r.declarations||!r.text_bytes||!r.diagnostics) return false;
  if(!foundation::checked_mul(r.modules,sizeof(HostModule))||
     !foundation::checked_mul(h.cleanup_errors,sizeof(CleanupError))) return false;
  return o.logging.record_capacity>0&&o.logging.record_capacity<=4096&&
    (o.logging.full==LogOverflow::DropOldest||o.logging.full==LogOverflow::RejectNewest)&&
    o.logging.minimum_level>=LogLevel::Trace&&o.logging.minimum_level<=LogLevel::Error;
}
thread_local detail::AdmissionFrame* frame_top=nullptr;
bool entered(const detail::HostControl* host) noexcept {
  for(auto* f=frame_top;f;f=f->previous) if(f->host==host) return true;
  return false;
}
struct FrameScope {
  detail::AdmissionFrame frame;
  explicit FrameScope(const detail::HostControl* h):frame{h,frame_top} { frame_top=&frame; }
  ~FrameScope(){foundation::invariant(frame_top==&frame);frame_top=frame.previous;}
};
}

namespace detail {
struct HostControl {
  struct Module { Name name;std::shared_ptr<ModuleLifecyclePort> lifecycle; };
  HostOptions options;
  policy::PolicyConfiguration config;
  HostPorts ports;
  HostIncarnation incarnation;
  mutable std::mutex mutex;
  std::condition_variable changed;
  HostPhase phase=HostPhase::Configuring;
  bool lifecycle=false,started=false,stopping=false,failed=false,policy_pending=false,logging_pending=false,log_closing=false;
  bool executions_pending=false,executors_pending=false;
  std::size_t active=0,started_count=0;
  std::optional<foundation::ErrorCode> primary;
  std::uint64_t cleanup_count=0;
  bool cleanup_saturated=false;
  std::vector<CleanupError> errors;
  std::vector<Module> modules;
  std::vector<std::size_t> pending_modules;
  std::unique_ptr<registry::RegistrationBatch> batch;
  std::shared_ptr<const registry::Catalog> catalog;
  policy::PolicyAssembly policy;
  std::shared_ptr<policy::ExecutionAccessSourcePort> source;
  std::shared_ptr<HostExecutionPort> executions;
  std::shared_ptr<LogPort> backend;
  std::shared_ptr<observability::MemoryDiagnostics> diagnostics;
  std::optional<observability::SafeLogger> logger;

  HostControl(HostOptions o,policy::PolicyConfiguration c,HostPorts p,HostIncarnation id)
      :options(o),config(std::move(c)),ports(std::move(p)),incarnation(id) {}
  bool drained() const noexcept {return active==0&&pending_modules.empty()&&!policy_pending&&!logging_pending&&
      !executions_pending&&!executors_pending;}
  void first_error(const Error& e) {
    std::lock_guard lock(mutex);if(!primary)primary=e.code();failed=true;
  }
  void cleanup_error(HostPhase at,std::optional<Name> module,const Error& e) {
    std::lock_guard lock(mutex);
    if(cleanup_count==(std::numeric_limits<std::uint64_t>::max)())cleanup_saturated=true;
    else ++cleanup_count;
    if(errors.size()<options.host.cleanup_errors)errors.push_back({at,module,e.code()});
  }
  void set_phase(HostPhase p) {std::lock_guard lock(mutex);phase=p;}
  void log(LogEvent event,std::optional<std::size_t> module={}) noexcept {
    if(!logger)return;
    LogField field{LogKey::Module,LogValueClass::PublicCode,module?*module+1:0};
    auto level=event==LogEvent::StartFailed?LogLevel::Error:LogLevel::Info;
    logger->try_write({level,LogComponent::Host,event,module?std::span<const LogField>(&field,1):std::span<const LogField>{}});
  }
  ShutdownReport report(ShutdownDisposition disposition,bool expired,std::span<PendingCleanup> output) const {
    std::lock_guard lock(mutex);
    std::size_t total=0,written=0;
    auto append=[&](PendingKind kind,std::optional<Name> module,std::size_t admissions=0) {
      if(written<output.size())output[written++]={kind,module,admissions};++total;
    };
    if(active)append(PendingKind::Admissions,{},active);
    if(executions_pending)append(PendingKind::Executions,{});
    if(executors_pending)append(PendingKind::Executors,{});
    if(policy_pending)append(PendingKind::PolicyStore,{});
    for(auto it=pending_modules.rbegin();it!=pending_modules.rend();++it)append(PendingKind::Module,modules[*it].name);
    if(logging_pending)append(PendingKind::Logging,{});
    const bool completed=disposition==ShutdownDisposition::Complete||disposition==ShutdownDisposition::CompleteWithErrors;
    const bool quiet=drained()&&((completed&&(phase==HostPhase::Stopped||phase==HostPhase::Failed))||
      (!lifecycle&&(phase==HostPhase::Stopped||phase==HostPhase::Failed||phase==HostPhase::Constructed||phase==HostPhase::Configuring)));
    return {disposition,phase,quiet,expired,active,pending_modules.size(),primary,cleanup_count,total,written,written<total};
  }
  bool close_policy() {
    if(!policy_pending)return true;
    try {
      auto result=policy.administration->close_store();
      if(!result){cleanup_error(HostPhase::Finalize,{},result.error());return false;}
      std::lock_guard lock(mutex);policy_pending=false;return true;
    } catch(const std::bad_alloc&) {cleanup_error(HostPhase::Finalize,{},host_error(HostErrc::BudgetExceeded));}
      catch(...) {cleanup_error(HostPhase::Finalize,{},host_error(HostErrc::CallbackException));}
    return false;
  }
  enum class CloseStep { Done,Pending,Expired };
  CloseStep close_logging(TimePoint deadline) {
    if(!logging_pending)return CloseStep::Done;
    if(!log_closing){log_closing=true;log(LogEvent::LogClosing);}
    if(std::chrono::steady_clock::now()>=deadline)return CloseStep::Expired;
    try {
      // facade分配失败仍由已登记的backend owner承担真实关闭责任。
      auto result=logger?logger->close():backend->close();
      if(!result){cleanup_error(HostPhase::StopModulesObservers,{},result.error());return CloseStep::Pending;}
      // 启动因外来流身份失败时，仍须接受该实际后端自身的合法关闭回执。
      if(result->covered_through.host.empty()||!result->covered_through.stream||!result->volatile_only||
         result->evicted_through>result->covered_through.accepted_sequence) {
        cleanup_error(HostPhase::StopModulesObservers,{},host_error(HostErrc::LoggingUnavailable));return CloseStep::Pending;
      }
      std::lock_guard lock(mutex);logging_pending=false;return CloseStep::Done;
    } catch(const std::bad_alloc&) {cleanup_error(HostPhase::StopModulesObservers,{},host_error(HostErrc::BudgetExceeded));}
      catch(...) {cleanup_error(HostPhase::StopModulesObservers,{},host_error(HostErrc::CallbackException));}
    return CloseStep::Pending;
  }
  ShutdownReport finish(TimePoint deadline,std::span<PendingCleanup> output,bool startup_failure) {
    auto expired=[&]{return std::chrono::steady_clock::now()>=deadline;};
    auto stopped=[&] {
      set_phase(startup_failure?HostPhase::Failed:HostPhase::Stopped);
      return report(cleanup_count?ShutdownDisposition::CompleteWithErrors:ShutdownDisposition::Complete,expired(),output);
    };
    auto timeout=[&]{return report(ShutdownDisposition::DeadlineExceeded,true,output);};
    auto incomplete=[&]{return report(ShutdownDisposition::NotQuiescent,expired(),output);};
    if(drained())return stopped();
    if(expired())return timeout();
    if(executions_pending) {
      set_phase(HostPhase::CancelOrFinish);
      try {
        executions->stop_accepting();
        auto result=executions->finish_until(deadline);
        if(!result){cleanup_error(HostPhase::CancelOrFinish,{},result.error());return incomplete();}
        if(!*result)return expired()?timeout():incomplete();
        {std::lock_guard lock(mutex);executions_pending=false;}
      } catch(...) {cleanup_error(HostPhase::CancelOrFinish,{},host_error(HostErrc::CallbackException));return incomplete();}
    }
    if(!startup_failure&&policy_pending) {
      set_phase(HostPhase::Finalize);
      if(!close_policy())return incomplete();
      if(drained())return stopped();if(expired())return timeout();
    }
    set_phase(HostPhase::DrainExecutors);
    if(executors_pending) {
      if(expired())return timeout();
      try {
        auto result=executions->drain_executors_until(deadline);
        if(!result){cleanup_error(HostPhase::DrainExecutors,{},result.error());return incomplete();}
        if(!*result)return expired()?timeout():incomplete();
        {std::lock_guard lock(mutex);executors_pending=false;}
      } catch(...) {cleanup_error(HostPhase::DrainExecutors,{},host_error(HostErrc::CallbackException));return incomplete();}
    }
    set_phase(HostPhase::StopModulesObservers);
    while(!pending_modules.empty()) {
      if(expired())return timeout();
      auto index=pending_modules.back();
      ModuleStopResult result{false,{}};
      try {result=modules[index].lifecycle->stop();}
      catch(const std::bad_alloc&) {result.error=host_error(HostErrc::BudgetExceeded);}
      catch(...) {result.error=host_error(HostErrc::CallbackException);}
      if(result.error)cleanup_error(HostPhase::StopModulesObservers,modules[index].name,*result.error);
      if(!result.quiescent)return incomplete();
      {std::lock_guard lock(mutex);pending_modules.pop_back();}
      if(drained())return stopped();if(expired())return timeout();
      log(LogEvent::ModuleStopped,index);
      if(expired())return timeout();
    }
    if(startup_failure&&policy_pending) {
      if(expired())return timeout();
      if(!close_policy())return incomplete();
      if(drained())return stopped();if(expired())return timeout();
    }
    if(logging_pending) {
      if(expired())return timeout();
      auto close=close_logging(deadline);
      if(close==CloseStep::Expired)return timeout();
      if(close==CloseStep::Pending)return incomplete();
      if(drained())return stopped();if(expired())return timeout();
    }
    set_phase(HostPhase::ReleaseStorage);
    return stopped();
  }
};

SubmitReply submit(const std::shared_ptr<HostControl>& host,
    std::shared_ptr<executions::detail::InvocationRecordBase> record) {
  std::shared_ptr<HostExecutionPort> backend;
  {std::lock_guard lock(host->mutex);backend=host->executions;}
  if(!backend)return Rejected{host_error(HostErrc::UnsupportedCapability)};
  return backend->submit(std::move(record));
}
SubmitReply submit_child(const std::shared_ptr<HostControl>& host,
    std::shared_ptr<executions::detail::InvocationRecordBase> record,std::shared_ptr<ExecutionScopePort> scope) {
  std::shared_ptr<HostExecutionPort> backend;
  {std::lock_guard lock(host->mutex);backend=host->executions;}
  if(!backend)return Rejected{host_error(HostErrc::UnsupportedCapability)};
  return backend->submit_child(std::move(record),std::move(scope));
}
HostAdmission::HostAdmission(std::shared_ptr<HostControl> host):owner_(std::move(host)) {
  std::lock_guard lock(owner_->mutex);
  if(owner_->phase!=HostPhase::Ready) {
    error_=host_error(owner_->stopping?HostErrc::HostDraining:HostErrc::NotReady);return;
  }
  if(owner_->active>=owner_->options.host.active_admissions){error_=host_error(HostErrc::Busy);return;}
  ++owner_->active;frame_={owner_.get(),frame_top};frame_top=&frame_;
}
HostAdmission::~HostAdmission() {
  if(error_)return;
  foundation::invariant(frame_top==&frame_);frame_top=frame_.previous;
  {std::lock_guard lock(owner_->mutex);foundation::invariant(owner_->active!=0);--owner_->active;}
  owner_->changed.notify_all();
}
}

NativeHost::NativeHost(std::shared_ptr<detail::HostControl> s) noexcept:state_(std::move(s)) {}
Result<std::unique_ptr<NativeHost>> NativeHost::create(const HostOptions& options,
    const policy::PolicyConfiguration& config,const HostPorts& ports) {
  try {
    if(!budgets(options))return failure<std::unique_ptr<NativeHost>>(HostErrc::BudgetExceeded);
    if(!owned(ports.authentication)||!owned(ports.clock)||!owned(ports.group_digest)||!owned(ports.threads)||
       (ports.logging_factory&&!owned(ports.logging_factory))||
       (ports.execution_factory&&!owned(ports.execution_factory)))return failure<std::unique_ptr<NativeHost>>(HostErrc::InvalidOwner);
    auto valid=policy::detail::validate_configuration(options.policy,config);
    if(!valid)return make_unexpected(valid.error());
    auto native_valid=invocation::detail::validate_budget(options.native);
    if(!native_valid)return make_unexpected(native_valid.error());
    HostIncarnation id{};
    if(BCryptGenRandom(nullptr,id.bytes.data(),static_cast<ULONG>(id.bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||id.empty())
      return failure<std::unique_ptr<NativeHost>>(HostErrc::IdentityUnavailable);
    auto batch=registry::RegistrationBatch::create(options.registration);
    if(!batch)return make_unexpected(batch.error());
    auto state=std::make_shared<detail::HostControl>(options,config,ports,id);
    state->modules.reserve(options.registration.modules);
    state->pending_modules.reserve(options.registration.modules);
    state->errors.reserve(options.host.cleanup_errors);
    state->batch=std::move(*batch);
    state->source=std::make_shared<NoExecutions>(id);
    return std::unique_ptr<NativeHost>(new NativeHost(std::move(state)));
  } catch(const std::bad_alloc&) {return failure<std::unique_ptr<NativeHost>>(HostErrc::BudgetExceeded);}
    catch(...) {return failure<std::unique_ptr<NativeHost>>(HostErrc::CallbackException);}
}

Result<void> NativeHost::add(const HostModule& module) {
  auto s=state_;
  {std::lock_guard lock(s->mutex);
    if(s->phase!=HostPhase::Configuring||s->started||s->lifecycle)return failure<void>(HostErrc::AlreadyStarted);
    if(s->failed)return failure<void>(HostErrc::InvalidInput);
    s->lifecycle=true;
  }
  struct Guard {detail::HostControl& s;~Guard(){std::lock_guard lock(s.mutex);s.lifecycle=false;}} guard{*s};
  FrameScope frame(s.get());
  try {
    if(!owned(module.lifecycle)) {auto e=host_error(HostErrc::InvalidOwner);s->first_error(e);return make_unexpected(e);}
    const auto& m=module.registration;
    if(!m.providers.empty()||!m.manifest.required_providers.empty()||
       ((!m.resources.empty()||!m.manifest.required_resources.empty())&&!s->ports.execution_factory)) {
      auto e=host_error(HostErrc::UnsupportedCapability);s->first_error(e);return make_unexpected(e);
    }
    auto result=s->batch->add(m);
    if(!result){s->first_error(result.error());return result;}
    {std::lock_guard lock(s->mutex);s->modules.push_back({m.manifest.name,module.lifecycle});}
    return {};
  } catch(const std::bad_alloc&) {auto e=host_error(HostErrc::BudgetExceeded);s->first_error(e);return make_unexpected(e);}
    catch(...) {auto e=host_error(HostErrc::CallbackException);s->first_error(e);return make_unexpected(e);}
}

Result<void> NativeHost::start() {
  auto s=state_;
  {std::lock_guard lock(s->mutex);
    if(s->lifecycle)return failure<void>(HostErrc::Busy);
    if(s->started||s->phase!=HostPhase::Configuring)return failure<void>(HostErrc::AlreadyStarted);
    s->lifecycle=true;s->started=true;s->phase=HostPhase::Validating;
  }
  struct Guard {detail::HostControl& s;~Guard(){std::lock_guard lock(s.mutex);s.lifecycle=false;}} guard{*s};
  FrameScope frame(s.get());
  auto fail=[&](Error e)->Result<void> {
    s->first_error(e);s->log(LogEvent::StartFailed);s->set_phase(HostPhase::Failed);
    s->finish(std::chrono::steady_clock::now()+s->options.host.failed_start_cleanup,{},true);
    s->set_phase(HostPhase::Failed);return make_unexpected(e);
  };
  try {
    if(s->failed)return fail(host_error(HostErrc::InvalidInput));
    auto catalog=s->batch->publish();
    if(!catalog)return fail(catalog.error());
    s->catalog=*catalog;
    for(std::size_t i=0;i<s->catalog->size();++i) {
      auto d=s->catalog->describe(static_cast<std::uint32_t>(i));
      if(!d)return fail(d.error());
      const auto& execution=(*d)->description().execution;
      if((*d)->shape()!=Shape::Read||(execution.requires_external_wait&&!(*d)->asynchronous_read())||(*d)->provider()||
         ((!execution.inline_safe||execution.requires_async_dispatch||(*d)->asynchronous_read())&&!s->ports.execution_factory))
        return fail(host_error(HostErrc::UnsupportedCapability));
    }
    if(s->ports.logging_factory) {
      auto result=s->ports.logging_factory->create(s->incarnation,s->options.logging);
      if(!result)return fail(result.error());
      if(!owned(*result))return fail(host_error(HostErrc::InvalidOwner));
      s->backend=std::move(*result);
    } else {
      auto result=observability::make_memory_logging(s->incarnation,1,s->options.logging);
      if(!result)return fail(result.error());
      s->backend=std::move(result->writer);
      {std::lock_guard lock(s->mutex);s->diagnostics=std::move(result->diagnostics);}
    }
    {std::lock_guard lock(s->mutex);s->logging_pending=true;}
    auto logger=observability::SafeLogger::create(s->backend);
    if(!logger)return fail(logger.error());
    s->logger.emplace(std::move(*logger));
    auto snapshot=s->logger->snapshot();
    if(!snapshot)return fail(snapshot.error());
    if(snapshot->accepted_through.host!=s->incarnation||snapshot->accepted_through.stream!=1||
       snapshot->accepted_through.accepted_sequence||snapshot->evicted_through||snapshot->retained_count||snapshot->state!=LogState::Open||
       snapshot->limits.record_capacity!=s->options.logging.record_capacity||snapshot->limits.full!=s->options.logging.full||
       snapshot->limits.minimum_level!=s->options.logging.minimum_level)
      return fail(host_error(HostErrc::LoggingUnavailable));
    s->log(LogEvent::Configured);s->log(LogEvent::Starting);
    if(s->ports.execution_factory) {
      auto made=s->ports.execution_factory->create(s->incarnation);
      if(!made)return fail(made.error());
      if(!owned(*made))return fail(host_error(HostErrc::InvalidOwner));
      // 从取得 owner 开始登记清理责任，观察源验证失败也必须实际排空。
      {std::lock_guard lock(s->mutex);s->executions=std::move(*made);
        s->executions_pending=true;s->executors_pending=true;}
      auto source=s->executions->observations();
      if(!owned(source)||source->identity().host!=s->incarnation)
        return fail(host_error(HostErrc::InvalidOwner));
      s->source=std::move(source);
    }
    auto policy=policy::PolicyStore::create(s->options.policy,s->config,s->ports.authentication,s->ports.clock,s->ports.group_digest,s->source);
    if(!policy)return fail(policy.error());
    s->policy=std::move(*policy);
    {std::lock_guard lock(s->mutex);s->policy_pending=true;}
    s->set_phase(HostPhase::Recovering);s->set_phase(HostPhase::Starting);
    for(const auto& name:s->catalog->module_order()) {
      auto module=std::find_if(s->modules.begin(),s->modules.end(),[&](const auto& m){return m.name==name;});
      foundation::invariant(module!=s->modules.end());
      auto result=module->lifecycle->start({*s->logger,name});
      if(!result)return fail(result.error());
      auto index=static_cast<std::size_t>(module-s->modules.begin());
      {std::lock_guard lock(s->mutex);s->pending_modules.push_back(index);++s->started_count;}
      s->log(LogEvent::ModuleStarted,index);
    }
    s->set_phase(HostPhase::Ready);s->log(LogEvent::Ready);return {};
  } catch(const std::bad_alloc&) {return fail(host_error(HostErrc::BudgetExceeded));}
    catch(...) {return fail(host_error(HostErrc::CallbackException));}
}

Result<HostSession> NativeHost::open(const policy::AuthenticationAttempt& attempt,const policy::DelegationInput& delegation) {
  auto s=state_;detail::HostAdmission admission(s);
  if(!admission)return make_unexpected(admission.error());
  try {
    auto authority=s->policy.store->open(attempt,delegation);
    if(!authority)return make_unexpected(authority.error());
    auto engine=invocation::NativeEngine::create(s->catalog,*authority,s->ports.threads,s->options.native);
    if(!engine)return make_unexpected(engine.error());
    return HostSession{std::make_shared<detail::SessionState>(detail::SessionState{s,*authority,*engine})};
  } catch(const std::bad_alloc&){return failure<HostSession>(HostErrc::BudgetExceeded);}
    catch(...){return failure<HostSession>(HostErrc::CallbackException);}
}

ShutdownReport NativeHost::shutdown_until(TimePoint deadline,std::span<PendingCleanup> pending) {
  auto s=state_;
  if(entered(s.get()))return s->report(ShutdownDisposition::Reentrant,false,pending);
  std::shared_ptr<HostExecutionPort> executions;
  {std::lock_guard lock(s->mutex);executions=s->executions;}
  if(executions&&executions->in_execution_thread())return s->report(ShutdownDisposition::Reentrant,false,pending);
  bool busy=false,stopped=false;
  {std::lock_guard lock(s->mutex);
    busy=s->lifecycle;
    stopped=s->phase==HostPhase::Stopped;
    if(!busy&&!stopped){s->lifecycle=true;s->stopping=true;}
  }
  if(busy)return s->report(ShutdownDisposition::Busy,false,pending);
  if(stopped)return s->report(s->cleanup_count?ShutdownDisposition::CompleteWithErrors:ShutdownDisposition::Complete,false,pending);
  struct Guard {detail::HostControl& s;~Guard(){std::lock_guard lock(s.mutex);s.lifecycle=false;}} guard{*s};
  FrameScope frame(s.get());
  bool stop_event=false;
  {std::lock_guard lock(s->mutex);stop_event=s->phase==HostPhase::Ready;if(stop_event)s->phase=HostPhase::StopAccepting;}
  if(stop_event&&std::chrono::steady_clock::now()<deadline)s->log(LogEvent::StopAccepting);
  if(executions) {
    try {executions->stop_accepting();}
    catch(...) {s->cleanup_error(HostPhase::StopAccepting,{},host_error(HostErrc::CallbackException));
      return s->report(ShutdownDisposition::NotQuiescent,false,pending);}
  }
  bool timed_out=false;
  {std::unique_lock lock(s->mutex);
    s->phase=HostPhase::CancelOrFinish;
    if(s->active&&!s->changed.wait_until(lock,deadline,[&]{return s->active==0;}))timed_out=true;
  }
  if(timed_out)return s->report(ShutdownDisposition::DeadlineExceeded,true,pending);
  return s->finish(deadline,pending,s->failed);
}

HostSnapshot NativeHost::snapshot(std::span<CleanupError> out) const noexcept {
  auto s=state_;std::lock_guard lock(s->mutex);
  auto written=std::min(out.size(),s->errors.size());
  std::copy_n(s->errors.begin(),written,out.begin());
  auto quiet=s->drained()&&(s->phase==HostPhase::Stopped||s->phase==HostPhase::Failed||
    (!s->lifecycle&&(s->phase==HostPhase::Constructed||s->phase==HostPhase::Configuring)));
  return {s->phase,quiet,s->active,s->started_count,s->pending_modules.size(),s->primary,s->cleanup_count,written,
          s->cleanup_count>s->errors.size()||written<s->errors.size(),s->cleanup_saturated};
}
HostCapabilities NativeHost::capabilities() const noexcept {return {};}
HostIncarnation NativeHost::incarnation() const noexcept {return state_->incarnation;}
Result<observability::LogReadPage> NativeHost::copy_logs(LogPosition after,std::span<PublicLogRecord> out) {
  auto s=state_;std::shared_ptr<observability::MemoryDiagnostics> owner;
  {std::lock_guard lock(s->mutex);owner=s->diagnostics;}
  if(!owner)return failure<observability::LogReadPage>(s->ports.logging_factory?HostErrc::UnsupportedCapability:HostErrc::LoggingUnavailable);
  try{return owner->copy_records(after,out);}catch(const std::bad_alloc&){return failure<observability::LogReadPage>(HostErrc::BudgetExceeded);}
    catch(...){return failure<observability::LogReadPage>(HostErrc::CallbackException);}
}
NativeHost::~NativeHost() {
  auto s=state_;std::lock_guard lock(s->mutex);
  foundation::invariant(!s->lifecycle&&s->drained()&&
    (s->phase==HostPhase::Stopped||s->phase==HostPhase::Failed||s->phase==HostPhase::Constructed||s->phase==HostPhase::Configuring));
}
HostSession::HostSession(std::shared_ptr<detail::SessionState> s) noexcept:state_(std::move(s)) {}
HostSession::HostSession(HostSession&&) noexcept=default;
HostSession& HostSession::operator=(HostSession&&) noexcept=default;
HostSession::~HostSession()=default;
Result<HostSession::CatalogContext> HostSession::catalog_context() const {
  auto s = state_;
  if(!s) return make_unexpected(host_error(HostErrc::InvalidSession));
  detail::HostAdmission admission(s->host);
  if(!admission) return make_unexpected(admission.error());
  return CatalogContext{s->host->catalog,s->authority};
}
Result<std::shared_ptr<const policy::VerifiedCaller>> HostSession::verify(const CallerDescription& caller) {
  auto s=state_;if(!s)return failure<std::shared_ptr<const policy::VerifiedCaller>>(HostErrc::InvalidSession);
  detail::HostAdmission admission(s->host);if(!admission)return make_unexpected(admission.error());
  return s->authority->verify(caller);
}
Result<ErasedExecutionResult> HostSession::result_erased(const policy::VerifiedCaller& caller,
    ExecutionRef ref,CppTypeToken type) const {
  auto s=state_;if(!s)return failure<ErasedExecutionResult>(HostErrc::InvalidSession);
  detail::HostAdmission admission(s->host);
  if(!admission)return make_unexpected(admission.error());
  std::shared_ptr<HostExecutionPort> backend;
  {std::lock_guard lock(s->host->mutex);backend=s->host->executions;}
  if(!backend)return failure<ErasedExecutionResult>(HostErrc::UnsupportedCapability);
  return backend->result(caller,s->authority,ref,type);
}
Result<ExecutionWaitReply> HostSession::wait(const policy::VerifiedCaller& caller,
    ExecutionRef ref,TimePoint deadline,std::stop_token stop) const {
  auto s=state_;if(!s)return failure<ExecutionWaitReply>(HostErrc::InvalidSession);
  detail::HostAdmission admission(s->host);
  if(!admission)return make_unexpected(admission.error());
  std::shared_ptr<HostExecutionPort> backend;
  {std::lock_guard lock(s->host->mutex);backend=s->host->executions;}
  if(!backend)return failure<ExecutionWaitReply>(HostErrc::UnsupportedCapability);
  return backend->wait(caller,s->authority,s->host->ports.threads,ref,deadline,stop);
}
Result<CancelDisposition> HostSession::cancel(const policy::VerifiedCaller& caller,ExecutionRef ref) const {
  auto s=state_;if(!s)return failure<CancelDisposition>(HostErrc::InvalidSession);
  detail::HostAdmission admission(s->host);
  if(!admission)return make_unexpected(admission.error());
  std::shared_ptr<HostExecutionPort> backend;
  {std::lock_guard lock(s->host->mutex);backend=s->host->executions;}
  if(!backend)return failure<CancelDisposition>(HostErrc::UnsupportedCapability);
  return backend->cancel(caller,s->authority,ref);
}
Result<void> HostSession::restrict_delegation(const policy::DelegationInput& delegation) {
  auto s=state_;if(!s)return failure<void>(HostErrc::InvalidSession);
  detail::HostAdmission admission(s->host);if(!admission)return make_unexpected(admission.error());
  return s->authority->restrict_delegation(delegation);
}
Result<void> HostSession::close() {
  auto s=state_;if(!s)return failure<void>(HostErrc::InvalidSession);
  return s->authority->close();
}
}
