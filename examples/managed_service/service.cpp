#include "service.hpp"
#include "schemas.hpp"
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/control/subscription_methods.hpp>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
namespace managed {
namespace {
using namespace runtime;
std::vector<policy::ScopeRule> rules() {
  std::vector<policy::ScopeRule> result;
  for(auto use:{policy::AccessUse::Invoke,policy::AccessUse::Catalog,policy::AccessUse::GetSummary,
      policy::AccessUse::Wait,policy::AccessUse::CancelExecution,policy::AccessUse::ReadResult,policy::AccessUse::ListSummary,policy::AccessUse::Subscribe})
    result.push_back({use,policy::OperationSelector{operation(),{}},{name("sample.use")},{target()},{principal()},
        {policy::SummaryField::Identity,policy::SummaryField::Owner,policy::SummaryField::Parent,
         policy::SummaryField::Phase,policy::SummaryField::Progress,policy::SummaryField::Facts}});
  return result;
}
struct Lifetime final:contracts::PortLifetime {};
policy::PolicyConfiguration configuration() {
  auto allow=rules();std::vector<policy::UsePolicyInput> views;
  for(auto use:{policy::AccessUse::Catalog,policy::AccessUse::GetSummary,policy::AccessUse::Wait,
      policy::AccessUse::CancelExecution,policy::AccessUse::ReadResult,policy::AccessUse::ListSummary,policy::AccessUse::Subscribe})views.push_back({use,{name("sample.use")},allow});
  return {{{principal(),allow}},{{{operation(),{}},{name("sample.use")},allow,false}},std::move(views),{{target(),1,allow,std::make_shared<Lifetime>()}}};
}
struct Clock final:policy::ClockPort {policy::TimePoint now()const noexcept override{return std::chrono::steady_clock::now();}};
struct PageClock final:control::CursorClock {
  std::uint64_t utc_seconds()const noexcept override {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }
  std::uint64_t monotonic_seconds()const noexcept override {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
};
struct Authentication final:policy::TrustedAuthenticationPort {
  std::string allowed_sid;
  inline static thread_local const Authentication* verified=nullptr;
  struct Scope {
    const Authentication* previous;
    Scope(const Authentication& auth,const std::string& os_sid):previous(verified){verified=os_sid==auth.allowed_sid?&auth:nullptr;}
    ~Scope(){verified=previous;}
  };
  Result<policy::AuthenticatedIdentity> authenticate(const policy::AuthenticationAttempt& input) override {
    if(verified!=this||input.credential!=std::vector<std::byte>{std::byte{1}})
      return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::AuthenticationFailed));
    auto deadline=std::chrono::steady_clock::now()+std::chrono::hours(1);
    return policy::AuthenticatedIdentity{principal(),policy::PrincipalKind::User,{},{rules(),deadline,false},deadline};
  }
};
struct Threads final:invocation::TrustedThreadPort {
  std::weak_ptr<contracts::ExecutorControlPort> executor;
  inline static thread_local const Threads* registered=nullptr;
  struct Scope {
    const Threads* previous;
    explicit Scope(const Threads& value):previous(registered){registered=&value;}
    ~Scope(){registered=previous;}
  };
  Result<invocation::ThreadObservation> current()const noexcept override {
    if(auto pool=executor.lock();pool&&pool->in_worker())return invocation::ThreadObservation{invocation::ThreadRole::Worker,name("managed"),true};
    if(registered==this)return invocation::ThreadObservation{invocation::ThreadRole::Control,name("managed"),true};
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::ThreadRejected));
  }
};
struct Factory final:host::HostExecutionFactoryPort {
  std::shared_ptr<Threads> threads;
  std::weak_ptr<host::ExecutionObservationPort> events;
  explicit Factory(std::shared_ptr<Threads> value):threads(std::move(value)){}
  Result<std::shared_ptr<host::HostExecutionPort>> create(contracts::HostIncarnation id)override {
    auto made=cpu_pool::Executor::create({2,8});if(!made)return foundation::make_unexpected(made.error());
    std::shared_ptr<contracts::ExecutorControlPort> pool=std::move(*made);threads->executor=pool;
    host::ExecutionOptions options;options.active=8;options.subjects={{principal().principal_id,1,2}};
    options.limits.records=128;options.limits.terminal_records=32;options.limits.input_bytes=1024*1024;
    options.limits.reply_bytes=1024*1024;options.limits.terminal_bytes=1024*1024;options.limits.waiters=16;
    options.limits.observation_leases=64;
    auto backend=host::make_executions(id,pool,options);
    if(!backend)(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
    else events=(*backend)->observation_events();
    return backend;
  }
};
struct Digest final:policy::TrustedGroupDigestPort {
  Result<contracts::ContractDigest> fingerprint(const policy::GroupSnapshot&)override {
    return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::InvalidGroup));
  }
};
struct Executor final:contracts::ExecutorPort {
  Result<void> submit(std::unique_ptr<contracts::ReadyWork>)override {
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::SubmitRequired));
  }
};
struct Lifecycle final:host::ModuleLifecyclePort {
  Result<void> start(const host::ModuleContext&)override{return {};}
  host::ModuleStopResult stop()override{return {true,{}};}
};
// 验证消费者只计量真实首字节；不改变传输额度或仲裁结果。
struct ObservedSink final:control::FrameSink {
  std::shared_ptr<local_ipc::Connection> connection;
  std::shared_ptr<std::atomic<std::uint64_t>> started;
  ObservedSink(std::shared_ptr<local_ipc::Connection> c,std::shared_ptr<std::atomic<std::uint64_t>> n)
      :connection(std::move(c)),started(std::move(n)){}
  Result<void> queue_frame(std::span<const std::byte> bytes,control::FramePriority priority)override {
    return connection->queue_frame(bytes,priority);
  }
  Result<std::unique_ptr<control::FrameBufferReservation>> reserve_frame(std::size_t size,control::FramePriority priority)override {
    return connection->reserve_frame(size,priority);
  }
  control::ByteStart start_frame(control::FrameBufferReservation& reservation,std::span<const std::byte> bytes)noexcept override {
    auto result=connection->start_frame(reservation,bytes);
    if(result==control::ByteStart::Started) {
      auto value=started->load();while(value!=UINT64_MAX&&!started->compare_exchange_weak(value,value+1)){}
    }
    return result;
  }
  void close()noexcept override{connection->close();}
};
Result<Output> compute(const Input& input,contracts::WorkContext& context) {
  auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(input.delay_ms);
  while(std::chrono::steady_clock::now()<deadline&&!context.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return Output{input.amount+1,input.text,context.stop_requested()};
}
Result<std::size_t> targets(const Input&,std::span<foundation::ObjectId> out)noexcept {
  if(out.empty())return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));out[0]=target();return 1;
}
}
struct Service::State {
  std::unique_ptr<host::NativeHost> host;
  std::optional<control::RegisteredInvocation<Input,Output>> registered;
  std::shared_ptr<const catalog::Catalog> catalog;
  std::optional<control::CursorCodec> cursor;
  std::shared_ptr<Authentication> auth=std::make_shared<Authentication>();
  std::shared_ptr<Clock> clock=std::make_shared<Clock>();
  std::shared_ptr<Threads> threads=std::make_shared<Threads>();
  std::shared_ptr<Factory> factory=std::make_shared<Factory>(threads);
  std::mutex mutex;
  std::vector<std::shared_ptr<local_ipc::Connection>> connections;
  struct Observer {std::weak_ptr<host::HostSession> session;std::weak_ptr<control::SubscriptionConnection> subscription;};
  std::vector<Observer> observers;
  std::shared_ptr<std::atomic<std::uint64_t>> notification_started=std::make_shared<std::atomic<std::uint64_t>>(0);
  std::optional<foundation::ErrorCode> connection_error;
  std::string_view error_stage;
  void failed(std::string_view stage,foundation::ErrorCode code) {
    std::lock_guard lock(mutex);connection_error=code;error_stage=stage;
  }
  struct Context {
    std::shared_ptr<host::HostSession> session;
    std::optional<control::Router> router;
    std::vector<std::shared_ptr<control::ExecutionMethod>> queries;
    std::shared_ptr<control::ListMethod> list;
    std::shared_ptr<control::SubscriptionConnection> subscriptions;
    void close(){if(router)router->close();queries.clear();list.reset();subscriptions.reset();router.reset();session.reset();}
    ~Context(){close();}
  };
  Result<host::HostSession> open(const std::string& sid) {
    Authentication::Scope verified(*auth,sid);
    return host->open({{std::byte{1}}},{rules(),clock->now()+std::chrono::minutes(30),false});
  }
  Result<std::shared_ptr<Context>> compose(const local_ipc::PeerIdentity& peer,std::shared_ptr<local_ipc::Connection> connection) {
    auto opened=open(peer.sid());if(!opened)return foundation::make_unexpected(opened.error());
    auto context=std::make_shared<Context>();context->session=std::make_shared<host::HostSession>(std::move(*opened));
    auto caller=context->session->verify({principal(),{}, {}});if(!caller)return foundation::make_unexpected(caller.error());
    auto source=context->session->catalog_context();if(!source)return foundation::make_unexpected(source.error());
    auto methods=control::catalog_methods(catalog,source->authorization,target());if(!methods)return foundation::make_unexpected(methods.error());
    auto binding=control::InvocationBinding::bind<Input,Output>(*context->session,*registered,operation(),{},contracts::Shape::Read,*caller,
        std::array{target()},targets,name("managed.rpc"));if(!binding)return foundation::make_unexpected(binding.error());
    auto submit=control::InvokeMethod::create_submit({*binding},clock);if(!submit)return foundation::make_unexpected(submit.error());methods->push_back(*submit);
    auto transport=control::ProtocolObservationTransport::create(connection,control::FramePriority::Control);
    if(!transport)return foundation::make_unexpected(transport.error());
    std::vector<control::ExecutionResultBinding> results{control::ExecutionResultBinding::create<Output>(operation(),registered->output())};
    for(auto kind:{control::ExecutionMethod::Kind::Get,control::ExecutionMethod::Kind::Wait,control::ExecutionMethod::Kind::Cancel,control::ExecutionMethod::Kind::Result}) {
      auto method=control::ExecutionMethod::create(kind,context->session,*caller,*transport,results,clock);if(!method)return foundation::make_unexpected(method.error());
      auto schema=control::ExecutionMethod::parameters(kind);if(!schema)return foundation::make_unexpected(schema.error());
      methods->push_back({std::string(control::ExecutionMethod::name(kind)),std::move(*schema),*method});context->queries.push_back(*method);
    }
    auto incarnation=control::wire_text(host->incarnation());
    control::CursorContext page_context;page_context.host=incarnation;
    auto list=control::ListMethod::create(source->authorization,*caller,*transport,*cursor,std::move(page_context));
    if(!list)return foundation::make_unexpected(list.error());
    auto list_schema=binding::CompiledSchema::compile(
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:execution-list:1#/$defs/request/properties/params"})",observation_schemas());
    if(!list_schema)return foundation::make_unexpected(list_schema.error());
    context->list=*list;methods->push_back({"execution.list",std::move(*list_schema),*list});
    auto events=factory->events.lock();if(!events)return foundation::make_unexpected(host::host_error(host::HostErrc::UnsupportedCapability));
    auto notifications=control::ProtocolObservationTransport::create(std::make_shared<ObservedSink>(std::move(connection),notification_started));
    if(!notifications)return foundation::make_unexpected(notifications.error());
    auto subscriptions=control::SubscriptionConnection::create(source->authorization,*caller,events,*notifications,clock,host->incarnation());
    if(!subscriptions)return foundation::make_unexpected(subscriptions.error());
    context->subscriptions=std::move(*subscriptions);
    auto observed=control::subscription_methods(context->subscriptions,*caller,observation_schemas());
    if(!observed)return foundation::make_unexpected(observed.error());
    for(auto& method:*observed)methods->push_back(std::move(method));
    control::Hello hello{"sample.managed",incarnation,incarnation,{}, {},"managed",65536};
    auto router=control::Router::create(std::move(hello),*caller,std::move(*methods));if(!router)return foundation::make_unexpected(router.error());
    context->router.emplace(std::move(*router));
    {std::lock_guard lock(mutex);
      std::erase_if(observers,[](const auto& observer){return observer.session.expired();});
      if(observers.size()>=8)return foundation::make_unexpected(host::host_error(host::HostErrc::BudgetExceeded));
      observers.push_back({context->session,context->subscriptions});}
    return context;
  }
};
Result<std::unique_ptr<Service>> Service::create(std::string sid) {
  auto state=std::make_shared<State>();state->auth->allowed_sid=std::move(sid);
  host::HostOptions options;options.policy.sessions=16;options.policy.inline_bindings=32;options.policy.frame_bytes=65536;
  options.policy.watches_per_session=8;options.policy.watches_per_principal=32;options.policy.active_watches=64;
  options.policy.queued_frames=256;options.policy.control_reserved_frames=16;options.policy.control_reserved_bytes=65536;
  options.policy.send_coordinators=48;
  auto host=host::NativeHost::create(options,configuration(),{state->auth,state->clock,std::make_shared<Digest>(),state->threads,{},state->factory});
  if(!host)return foundation::make_unexpected(host.error());state->host=std::move(*host);
  auto service=std::unique_ptr<Service>(new Service(state));
  registry::ModuleManifest manifest{name("managed"),version()};manifest.operations.push_back(operation());manifest.executors.push_back(name("cpu"));
  registry::ModuleInput module{manifest,{}, {}, {}, {},{{name("cpu"),name("managed"),false,false,std::make_shared<Executor>()}}, {}};
  module.register_operations=[](registry::Registrar& registrar) {
    registry::SubmissionStorage<Input,Output> storage{16384,16384,
      [](const Input& input)->Result<std::size_t>{return sizeof(Input)+input.text.capacity();},
      [](const contracts::InvokeReply<Output>& reply)->Result<std::size_t>{
        auto completed=std::get_if<contracts::Completed<Output>>(&reply);if(!completed)return sizeof(reply);
        auto read=std::get_if<contracts::ReadCompleted<Output>>(&completed->outcome.value());return sizeof(reply)+(read&&read->result?read->result->text.capacity():0);
      }};
    contracts::DefinitionInput definition{operation(),{},{false,false,false,name("cpu"),name("managed")},contracts::AtomicMode::PureCompute,{name("sample.use")},
      R"({"format":"ock.command-docs/1","purpose":"Bounded managed computation","counterexamples":["Negative amount is invalid"],"coordinates":"not applicable","position_mode":"not applicable","impact_scope":"returned value only","id_sources":"current host catalog","cancellation":"cooperative stop returns observed intent","retry":{"natural_idempotence":"yes","framework_deduplication":"not provided","device_deduplication":"not applicable"},"durability":"Volatile","result_phases":"ReadCompleted or rejected before start","error_repair":"correct input and retry"})"};
    auto added=registrar.compute(compute,definition,{{},{},{name("managed"),name("cpu")},{},false},storage);if(!added)throw std::runtime_error("Managed registration rejected");
  };
  auto added=state->host->add({std::move(module),std::make_shared<Lifecycle>()});if(!added)return foundation::make_unexpected(added.error());
  auto started=state->host->start();if(!started)return foundation::make_unexpected(started.error());
  auto cursor=control::CursorCodec::create(control::wire_text(state->host->incarnation()),std::make_shared<PageClock>());
  if(!cursor)return foundation::make_unexpected(cursor.error());state->cursor.emplace(std::move(*cursor));
  auto registered=control::RegisteredInvocation<Input,Output>::create();if(!registered)return foundation::make_unexpected(registered.error());state->registered.emplace(std::move(*registered));
  auto session=state->open(state->auth->allowed_sid);if(!session)return foundation::make_unexpected(session.error());
  auto source=session->catalog_context();if(!source)return foundation::make_unexpected(source.error());
  auto catalog=catalog::Catalog::create(source->definitions,{{contracts::TypeContract<Input>::identity(),state->registered->arguments().shared_schema()},
      {contracts::TypeContract<Output>::identity(),state->registered->output().record.shared_schema()}});
  if(!catalog)return foundation::make_unexpected(catalog.error());state->catalog=std::make_shared<const catalog::Catalog>(std::move(*catalog));
  auto closed=session->close();if(!closed)return foundation::make_unexpected(closed.error());return service;
}
Result<std::unique_ptr<local_ipc::Server>> Service::listen(std::string instance) {
  local_ipc::PipeOptions options;options.instance=std::move(instance);options.allowed_client_sids={state_->auth->allowed_sid};options.connections=8;
  auto state=state_;
  return local_ipc::Server::listen(options,[state](auto connection) {
    {std::lock_guard lock(state->mutex);std::erase_if(state->connections,[](const auto& c){return c->wait_closed(std::chrono::milliseconds(0));});state->connections.push_back(connection);}
    auto context=std::make_shared<std::shared_ptr<State::Context>>();std::weak_ptr<local_ipc::Connection> weak=connection;
    connection->start([state,weak,context](data::Payload payload) {
      auto connection=weak.lock();if(!connection)return;Threads::Scope thread(*state->threads);
      if(!*context) {
        auto peer=connection->peer();if(!peer){connection->close();return;}
        auto composed=state->compose(*peer,connection);if(!composed){state->failed("compose",composed.error().code());connection->close();return;}*context=std::move(*composed);
      }
      auto response=(*context)->router->dispatch(payload.view());if(!response){state->failed("dispatch",response.error().code());connection->close();return;}
      if(response->queued)return;
      if(response->json.empty()){connection->close();return;}
      auto value=data::Payload::parse(response->json);if(!value){connection->close();return;}
      auto frame=control::encode_frame(*value);if(!frame||!connection->send(*frame)){connection->close();return;}
      if(response->close){(*context)->router->close();connection->close_after_flush();}
    },[context]{if(*context)(*context)->close();context->reset();},[state,weak,context] {
      if(!*context)return;Threads::Scope thread(*state->threads);
      auto failed=[&](std::string_view stage,const auto& result){
        if(result)return false;state->failed(stage,result.error().code());if(auto connection=weak.lock())connection->close();return true;
      };
      if(auto events=state->factory->events.lock();events&&failed("events",events->pump(64)))return;
      for(const auto& query:(*context)->queries)if(failed("query",query->pump()))return;
      if(failed("list",(*context)->list->pump()))return;
      (void)failed("subscription",(*context)->subscriptions->pump());
    });
  });
}
Result<void> Service::shutdown() {
  auto state=state_;std::vector<std::shared_ptr<local_ipc::Connection>> connections;
  {std::lock_guard lock(state->mutex);connections.swap(state->connections);}
  for(const auto& connection:connections)connection->close();
  bool drained=true;for(const auto& connection:connections)drained=connection->wait_closed(std::chrono::seconds(5))&&drained;
  std::optional<foundation::ErrorCode> error;std::string_view stage;
  {std::lock_guard lock(state->mutex);error=std::exchange(state->connection_error,{});stage=state->error_stage;}
  if(error)std::cerr<<"Connection failure "<<stage<<' '<<error->domain().name()<<':'<<error->value()<<'\n';
  if(state->host)drained=state->host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5)).quiescent&&drained;
  if(!drained)return foundation::make_unexpected(host::host_error(host::HostErrc::BudgetExceeded));return {};
}
Service::ObservationDiagnostics Service::observation_diagnostics()const {
  auto state=state_;ObservationDiagnostics result;
  std::vector<std::shared_ptr<control::SubscriptionConnection>> subscriptions;
  {std::lock_guard lock(state->mutex);for(const auto& observer:state->observers)
    if(auto subscription=observer.subscription.lock())subscriptions.push_back(std::move(subscription));}
  for(const auto& subscription:subscriptions){++result.sessions;result.queued_bytes+=subscription->queued_bytes();}
  result.started=state->notification_started->load();return result;
}
Result<std::size_t> Service::restrict_observers() {
  auto state=state_;std::vector<std::shared_ptr<host::HostSession>> sessions;
  {std::lock_guard lock(state->mutex);for(const auto& observer:state->observers)
    if(auto session=observer.session.lock())sessions.push_back(std::move(session));}
  auto scope=rules();std::erase_if(scope,[](const auto& rule){return rule.use==policy::AccessUse::Subscribe;});
  for(const auto& session:sessions) {
    auto restricted=session->restrict_delegation({scope,state->clock->now()+std::chrono::minutes(5),false});
    if(!restricted)return foundation::make_unexpected(restricted.error());
  }
  return sessions.size();
}
Service::~Service(){(void)shutdown();}
}
