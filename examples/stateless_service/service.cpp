#include "service.hpp"
#include <mutex>
#include <iostream>
namespace stateless {
namespace {
using namespace runtime;
std::vector<policy::ScopeRule> rules() {
  return {{policy::AccessUse::Invoke,policy::OperationSelector{operation(),{}},{name("sample.use")},{target()},{principal()},{}},
          {policy::AccessUse::Catalog,policy::OperationSelector{operation(),{}},{name("sample.use")},{target()},{principal()},{}}};
}
struct Lifetime final : contracts::PortLifetime {};
policy::PolicyConfiguration configuration() {
  auto allow = rules();
  return {{{principal(),allow}},{{{operation(),{}},{name("sample.use")},allow,false}},
          {{policy::AccessUse::Catalog,{name("sample.use")},allow}},
          {{target(),1,allow,std::make_shared<Lifetime>()}}};
}
struct Clock final : policy::ClockPort { policy::TimePoint now() const noexcept override { return std::chrono::steady_clock::now(); } };
struct Authentication final : policy::TrustedAuthenticationPort {
  std::string allowed_sid;
  inline static thread_local const Authentication *verified = nullptr;
  struct Scope {
    const Authentication *previous;
    Scope(const Authentication &auth,const std::string &os_sid) : previous(verified) {
      verified = os_sid == auth.allowed_sid ? &auth : nullptr;
    }
    ~Scope() { verified = previous; }
  };
  Result<policy::AuthenticatedIdentity> authenticate(const policy::AuthenticationAttempt &attempt) override {
    if(verified != this || attempt.credential != std::vector<std::byte>{std::byte{1}}) return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::AuthenticationFailed));
    auto deadline = std::chrono::steady_clock::now()+std::chrono::hours(1);
    return policy::AuthenticatedIdentity{principal(),policy::PrincipalKind::User,{}, {rules(),deadline,false},deadline};
  }
};
struct Threads final : invocation::TrustedThreadPort {
  inline static thread_local const Threads *registered = nullptr;
  struct Scope {
    const Threads *previous;
    explicit Scope(const Threads &threads) : previous(registered) { registered = &threads; }
    ~Scope() { registered = previous; }
  };
  Result<invocation::ThreadObservation> current() const noexcept override {
    if(registered != this) return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::ThreadRejected));
    return invocation::ThreadObservation{invocation::ThreadRole::Control,name("sample.inline"),true};
  }
};
struct Digest final : policy::TrustedGroupDigestPort {
  Result<contracts::ContractDigest> fingerprint(const policy::GroupSnapshot &) override {
    return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::InvalidGroup));
  }
};
struct Executor final : contracts::ExecutorPort {
  Result<void> submit(std::unique_ptr<contracts::ReadyWork>) override {
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::SubmitRequired));
  }
};
struct Lifecycle final : host::ModuleLifecyclePort {
  Result<void> start(const host::ModuleContext &) override { return {}; }
  host::ModuleStopResult stop() override { return {true,{}}; }
};
Result<Value> increment(const Value &value,contracts::WorkContext &) { return Value{value.amount+1,value.text}; }
Result<std::size_t> targets(const Value &,std::span<foundation::ObjectId> out) noexcept {
  if(out.empty()) return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));
  out[0] = target(); return 1;
}
}
struct Service::State : std::enable_shared_from_this<Service::State> {
  std::unique_ptr<host::NativeHost> host;
  std::optional<control::RegisteredInvocation<Value,Value>> registered;
  std::shared_ptr<const catalog::Catalog> catalog;
  std::shared_ptr<Authentication> auth = std::make_shared<Authentication>();
  std::shared_ptr<Clock> clock = std::make_shared<Clock>();
  std::shared_ptr<Threads> threads = std::make_shared<Threads>();
  std::mutex mutex;
  std::vector<std::shared_ptr<local_ipc::Connection>> connections;
  struct Context {
    std::optional<host::HostSession> session;
    std::optional<control::Router> router;
    void close() { if(router) router->close(); if(session) session->close(); router.reset(); session.reset(); }
    ~Context() { close(); }
  };
  Result<host::HostSession> open(const std::string &sid) {
    Authentication::Scope verified(*auth,sid);
    // 非空格式标记满足认证端口输入合同；权限来自当前线程已核验的 OS 身份，不来自该公开字节。
    return host->open({{std::byte{1}}}, {rules(),clock->now()+std::chrono::minutes(30),false});
  }
  Result<std::shared_ptr<Context>> compose(const local_ipc::PeerIdentity &peer) {
    auto session = open(peer.sid());
    if(!session) return foundation::make_unexpected(session.error());
    auto context = std::make_shared<Context>(); context->session.emplace(std::move(*session));
    auto caller = context->session->verify({principal(),{}, {}});
    if(!caller) return foundation::make_unexpected(caller.error());
    auto source = context->session->catalog_context();
    if(!source) return foundation::make_unexpected(source.error());
    auto methods = control::catalog_methods(catalog,source->authorization,target());
    if(!methods) return foundation::make_unexpected(methods.error());
    auto binding = control::InvocationBinding::bind<Value,Value>(*context->session,*registered,operation(),{},contracts::Shape::Read,*caller,
        std::array{target()},targets,name("sample.rpc"));
    if(!binding) return foundation::make_unexpected(binding.error());
    auto invoke = control::InvokeMethod::create({*binding},clock);
    if(!invoke) return foundation::make_unexpected(invoke.error());
    methods->push_back(*invoke);
    const auto incarnation = control::wire_text(host->incarnation());
    control::Hello hello{"sample.stateless",incarnation,incarnation};
    hello.frame_bytes = local_ipc::PipeOptions{}.frame_bytes - 12;
    auto router = control::Router::create(std::move(hello),*caller,std::move(*methods));
    if(!router) return foundation::make_unexpected(router.error());
    context->router.emplace(std::move(*router));
    return context;
  }
};
Result<std::unique_ptr<Service>> Service::create(std::string allowed_sid) {
  auto state = std::make_shared<State>(); state->auth->allowed_sid = std::move(allowed_sid);
  auto host = host::NativeHost::create({},configuration(),{state->auth,state->clock,std::make_shared<Digest>(),state->threads,{}});
  if(!host) return foundation::make_unexpected(host.error());
  state->host = std::move(*host);
  registry::ModuleManifest manifest{name("sample"),version()};
  manifest.operations.push_back(operation()); manifest.executors.push_back(name("inline"));
  registry::ModuleInput module{manifest,{}, {}, {}, {}, {{name("inline"),name("sample.inline"),false,false,std::make_shared<Executor>()}}, {}};
  module.register_operations = [](registry::Registrar &registrar) {
    contracts::DefinitionInput definition{operation(),{}, {true,false,false,name("inline"),name("sample.inline")},
        contracts::AtomicMode::PureCompute,{name("sample.use")},
        R"({"format":"ock.command-docs/1","purpose":"Increment a bounded integer and preserve UTF-8 text","counterexamples":["Negative input is invalid","100 produces an invalid output"],"coordinates":"not applicable","position_mode":"not applicable","impact_scope":"returned value only","id_sources":"current host catalog","cancellation":"caller deadline before invocation","retry":{"natural_idempotence":"yes","framework_deduplication":"not provided","device_deduplication":"not applicable"},"durability":"not provided","result_phases":"ReadCompleted or FailedBeforeApply","error_repair":"correct input and retry"})"};
    auto added = registrar.compute(increment,definition,{{},{},{name("sample"),name("inline")},{},false});
    if(!added) throw std::runtime_error("sample registration rejected");
  };
  auto added = state->host->add({std::move(module),std::make_shared<Lifecycle>()});
  if(!added) return foundation::make_unexpected(added.error());
  auto started = state->host->start();
  if(!started) return foundation::make_unexpected(started.error());
  // 冻结 Host 的动态投影只创建一次；临时装配会话关闭后不保留其权限。
  auto registered=control::RegisteredInvocation<Value,Value>::create();
  if(!registered) return foundation::make_unexpected(registered.error());
  state->registered.emplace(std::move(*registered));
  auto session=state->open(state->auth->allowed_sid);
  if(!session) return foundation::make_unexpected(session.error());
  auto source=session->catalog_context();
  if(!source) return foundation::make_unexpected(source.error());
  auto catalog=catalog::Catalog::create(source->definitions,
      {{contracts::TypeContract<Value>::identity(),state->registered->arguments().shared_schema()}});
  if(!catalog) return foundation::make_unexpected(catalog.error());
  state->catalog=std::make_shared<const catalog::Catalog>(std::move(*catalog));
  auto closed=session->close();
  if(!closed) return foundation::make_unexpected(closed.error());
  return std::unique_ptr<Service>(new Service(std::move(state)));
}
Service::~Service() {
  close_connections();
  if(state_->host) state_->host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
}
Result<std::unique_ptr<local_ipc::Server>> Service::listen(std::string instance) {
  local_ipc::PipeOptions options; options.instance = std::move(instance); options.allowed_client_sids = {state_->auth->allowed_sid};
  auto state = state_;
  return local_ipc::Server::listen(options,[state](auto connection) {
    { std::lock_guard lock(state->mutex);
      std::erase_if(state->connections,[](auto &c) { return c->wait_closed(std::chrono::milliseconds(0)); });
      state->connections.push_back(connection);
    }
    auto context = std::make_shared<std::shared_ptr<State::Context>>();
    std::weak_ptr<local_ipc::Connection> weak = connection;
    connection->start([state,weak,context](data::Payload payload) {
      auto connection = weak.lock(); if(!connection) return;
      Threads::Scope thread(*state->threads);
      if(!*context) {
        auto peer = connection->peer(); if(!peer) { connection->close(); return; }
        auto composed = state->compose(*peer); if(!composed) {
          std::cerr << "Connection composition rejected: " << composed.error().code().domain().name() << ':' << composed.error().code().value() << '\n';
          connection->close(); return;
        }
        *context = std::move(*composed);
      }
      auto response = (*context)->router->dispatch(payload.view());
      if(!response || response->json.empty()) { connection->close(); return; }
      auto value = data::Payload::parse(response->json);
      auto frame = value ? control::encode_frame(*value) : Result<std::vector<std::byte>>(foundation::make_unexpected(value.error()));
      if(!frame || !connection->send(*frame)) { connection->close(); return; }
      // 错误响应允许已排队帧先交付；对端随后协议关闭，不接受第二请求。
      if(response->close) { (*context)->router->close(); connection->close_after_flush(); }
    },[context] { if(*context) (*context)->close(); context->reset(); });
  });
}
void Service::close_connections() {
  std::vector<std::shared_ptr<local_ipc::Connection>> connections;
  { std::lock_guard lock(state_->mutex); connections.swap(state_->connections); }
  for(auto &connection : connections) connection->close();
  for(auto &connection : connections) connection->wait_closed(std::chrono::seconds(5));
}
Result<data::Payload> Service::native(const Value &value) {
  auto sid = local_ipc::current_user_sid(); if(!sid) return foundation::make_unexpected(sid.error());
  auto session = state_->open(*sid); if(!session) return foundation::make_unexpected(session.error());
  auto caller = session->verify({principal(),{}, {}}); if(!caller) return foundation::make_unexpected(caller.error());
  Threads::Scope thread(*state_->threads);
  auto bound = session->bind<Value,Value>(operation(),{},contracts::Shape::Read,*caller,std::array{target()},targets,name("sample.native"));
  if(!bound) return foundation::make_unexpected(bound.error());
  auto reply = bound->invoke(value,{{},state_->clock->now()+std::chrono::seconds(1),100});
  return control::encode_invoke<Value>(reply,[&](const Value &out) { return state_->registered->output().encode(out); });
}
Result<void> Service::measure(MeasurementPort &meter) {
  auto sid=local_ipc::current_user_sid(); if(!sid) return foundation::make_unexpected(sid.error());
  auto session=state_->open(*sid); if(!session) return foundation::make_unexpected(session.error());
  auto caller=session->verify({principal(),{}, {}}); if(!caller) return foundation::make_unexpected(caller.error());
  Threads::Scope thread(*state_->threads);
  auto native=session->bind<Value,Value>(operation(),{},contracts::Shape::Read,*caller,std::array{target()},targets,name("sample.native.cost"));
  auto dynamic=binding::BoundOperation<Value,Value>::create(*session,state_->registered->arguments(),operation(),{},contracts::Shape::Read,*caller,std::array{target()},targets,name("sample.dynamic.cost"));
  if(!native) return foundation::make_unexpected(native.error());
  if(!dynamic) return foundation::make_unexpected(dynamic.error());
  auto input=data::Payload::parse(R"({"amount":4,"text":"small request"})");
  const Value value{4,"small request"};
  const runtime::invocation::InvokeOptions options{{},state_->clock->now()+std::chrono::seconds(20),100};
  auto valid=[](const contracts::InvokeReply<Value> &reply) {
    auto completed=std::get_if<contracts::Completed<Value>>(&reply);
    if(!completed) return false;
    auto read=std::get_if<contracts::ReadCompleted<Value>>(&completed->outcome.value());
    return read && read->result && read->result->amount==5 && read->result->text=="small request";
  };
  if(!valid(native->invoke(value,options)) || !valid(dynamic->invoke(input->view(),options)))
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidOutput));
  for(unsigned i=0;i<20;++i) {
    meter.begin("Native"); auto reply=native->invoke(value,options); meter.end();
    if(!valid(reply)) return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidOutput));
  }
  for(unsigned i=0;i<20;++i) {
    meter.begin("Dynamic"); auto reply=dynamic->invoke(input->view(),options); meter.end();
    if(!valid(reply)) return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidOutput));
  }
  return {};
}
}
