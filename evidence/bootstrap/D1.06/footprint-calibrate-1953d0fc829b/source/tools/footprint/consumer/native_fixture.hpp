#pragma once
#include "native_value.hpp"
#include <ock/runtime/host.hpp>
#include <ock/foundation/sdk_version.hpp>
#include <array>
#include <optional>
#include <stdexcept>
#include <thread>
namespace native_service {
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using namespace policy;
namespace foundation = ock::foundation;
void require(bool value) { if (!value) throw std::runtime_error("Native consumer contract failed"); }
Name name(const char* text) { return *Name::parse(text); }
OperationKey key(const char* text) { return {name(text), *OperationVersion::parse("1.0.0", 5)}; }
template <class T> T identity() { T value{}; value.bytes[15] = 1; return value; }
PrincipalRef principal() { return {identity<PrincipalId>()}; }
foundation::ObjectId target() { return identity<foundation::ObjectId>(); }
std::vector<ScopeRule> rules() {
  std::vector<ScopeRule> result;
  for (auto operation : {"native.compute", "native.read"})
    result.push_back({AccessUse::Invoke, OperationSelector{key(operation), {}},
                      {name("allow")}, {target()}, {principal()}, {}});
  return result;
}
struct Clock final : ClockPort {
  TimePoint now() const noexcept override { return std::chrono::steady_clock::now(); }
};
struct Authentication final : TrustedAuthenticationPort {
  TimePoint deadline;
  explicit Authentication(TimePoint end) : deadline(end) {}
  Result<AuthenticatedIdentity> authenticate(const AuthenticationAttempt& input) override {
    if (input.credential != std::vector<std::byte>{std::byte{7}})
      return make_unexpected(policy_error(PolicyErrc::AuthenticationFailed));
    return AuthenticatedIdentity{principal(), PrincipalKind::Service, {}, {rules(), deadline, false}, deadline};
  }
};
struct Digest final : TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const GroupSnapshot&) override {
    // 本消费者关闭 group；意外调用明确拒绝。
    return make_unexpected(policy_error(PolicyErrc::TargetUnavailable));
  }
};
struct Lifetime final : PortLifetime {};
struct Threads final : TrustedThreadPort {
  std::thread::id owner = std::this_thread::get_id();
  Result<ThreadObservation> current() const noexcept override {
    if (owner != std::this_thread::get_id())
      return make_unexpected(invocation_error(InvocationErrc::ThreadRejected));
    return ThreadObservation{ThreadRole::Application, name("app"), true};
  }
};
struct Executor final : ExecutorPort {
  Result<void> submit(std::unique_ptr<ReadyWork>) override {
    throw std::runtime_error("Native consumer unexpectedly created submitted work");
  }
};
struct Reader { int read() const { return 7; } };
unsigned entered = 0;
Result<Value> compute(const Value& value, WorkContext&) { ++entered; return Value{value.number + 2}; }
Result<Value> read(const Value& value, WorkContext&, ReadServices<Reader>& service) {
  ++entered; return Value{value.number + service.reader().read()};
}
Result<std::size_t> targets(const Value&, std::span<foundation::ObjectId> output) noexcept {
  if (output.empty()) return make_unexpected(error(ContractsErrc::BudgetExceeded));
  output[0] = target(); return 1;
}
int result(const InvokeReply<Value>& reply) {
  require(std::holds_alternative<Completed<Value>>(reply));
  const auto& value = std::get<Completed<Value>>(reply).outcome.value();
  require(std::holds_alternative<ReadCompleted<Value>>(value));
  const auto& result = std::get<ReadCompleted<Value>>(value).result;
  require(bool(result)); return result->number;
}
struct Proof {
  unsigned starts=0,stops=0,destroyed=0;
  LogPosition accepted{};
  bool flushed=false;
};
struct Lifecycle final : host::ModuleLifecyclePort {
  Proof* proof;
  explicit Lifecycle(Proof& value):proof(&value){}
  ~Lifecycle(){++proof->destroyed;}
  Result<void> start(const host::ModuleContext& context) override {
    ++proof->starts;
    std::array fields{LogField{LogKey::Count,LogValueClass::PublicCount,1}};
    auto written=context.log.try_write({LogLevel::Info,LogComponent::Host,LogEvent::Diagnostic,fields});
    require(written.decision==LogDecision::Accepted&&written.accepted.has_value());
    proof->accepted=*written.accepted;
    auto flushed=context.log.flush(proof->accepted);
    require(bool(flushed)&&flushed->covered_through==proof->accepted&&flushed->volatile_only);
    proof->flushed=true;return {};
  }
  host::ModuleStopResult stop() override {++proof->stops;return {true,{}};}
};

class Scenario {
  struct Owners {
    std::shared_ptr<Clock> clock;
    std::shared_ptr<Authentication> auth;
    std::shared_ptr<Lifecycle> lifecycle;
    std::shared_ptr<Reader> reader;
    std::shared_ptr<Lifetime> target_lifetime;
    std::unique_ptr<host::NativeHost> runtime;
    std::optional<host::HostSession> session;
    std::shared_ptr<const VerifiedCaller> caller;
    std::optional<host::HostBound<Value,Value>> computing,reading;
  };
  Proof proof_;
  std::weak_ptr<Lifecycle> lifecycle_;
  std::weak_ptr<Reader> reader_;
  std::weak_ptr<Lifetime> target_;
  std::unique_ptr<Owners> owners_;
  std::uint64_t log_upper_=0;
public:
  static constexpr const char* kind="native";
  explicit Scenario(){
    static_assert(ock::sdk::runtime_available&&ock::sdk::implementation_stage=="NativeSubset");
    owners_=std::make_unique<Owners>();auto& o=*owners_;
    o.clock=std::make_shared<Clock>();o.auth=std::make_shared<Authentication>(o.clock->now()+std::chrono::hours(1));
    o.target_lifetime=std::make_shared<Lifetime>();target_=o.target_lifetime;
    PolicyConfiguration config;
    config.principals.push_back({principal(),rules()});
    for(auto operation:{"native.compute","native.read"})
      config.operations.push_back({{key(operation),{}},{name("allow")},rules(),false});
    config.targets.push_back({target(),1,rules(),o.target_lifetime});
    auto made=host::NativeHost::create({},config,{o.auth,o.clock,std::make_shared<Digest>(),std::make_shared<Threads>(),{}});
    require(bool(made));o.runtime=std::move(*made);auto& runtime=*o.runtime;
    require(!runtime.incarnation().empty());
    const auto caps=runtime.capabilities();
    require(caps.native_read&&caps.native_compute&&caps.ordinary_memory_logging);
    require(!caps.async_execution&&!caps.execution_observation&&!caps.state&&!caps.storage&&!caps.restore);
    registry::ModuleManifest manifest{name("native"),*OperationVersion::parse("1.0.0",5)};
    manifest.operations={key("native.compute"),key("native.read")};manifest.services={name("reader")};manifest.executors={name("inline")};
    manifest.required_services.push_back({{name("native"),name("reader")},CppTypeToken::of<Reader>()});
    registry::ModuleInput module{manifest,{},{},{},{},{{name("inline"),name("app"),false,false,std::make_shared<Executor>()}},{}};
    o.reader=std::make_shared<Reader>();reader_=o.reader;
    auto service=registry::ServiceBinding::make(name("reader"),o.reader);require(bool(service));module.services.push_back(*service);
    module.register_operations=[](registry::Registrar& registrar){
      DefinitionInput definition{key("native.compute"),{},{true,false,false,name("inline"),name("app")},AtomicMode::PureCompute,{name("allow")},"NativeSubset footprint"};
      registry::OperationOptions options{{},{},{name("native"),name("inline")},{},false};
      require(bool(registrar.compute(compute,definition,options)));
      definition.key=key("native.read");definition.atomic_mode=AtomicMode::Incompatible;
      options.read_service=registry::ServiceRef{name("native"),name("reader")};
      require(bool(registrar.read(read,definition,options)));
    };
    o.lifecycle=std::make_shared<Lifecycle>(proof_);lifecycle_=o.lifecycle;
    require(bool(runtime.add({module,o.lifecycle})));
    require(!runtime.open({{std::byte{7}}},{rules(),o.auth->deadline,false})&&entered==0);
    require(bool(runtime.start())&&proof_.starts==1&&proof_.flushed);
    require(runtime.snapshot({}).phase==host::HostPhase::Ready);
  }
  ~Scenario(){if(owners_&&owners_->runtime)(void)owners_->runtime->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));}
  void prepare(){
    auto& o=*owners_;auto& runtime=*o.runtime;
    require(proof_.accepted.host==runtime.incarnation()&&proof_.accepted.stream==1);
    std::array<PublicLogRecord,64> records{};
    auto page=runtime.copy_logs({runtime.incarnation(),1,0},records);
    require(bool(page)&&page->copied==5&&page->accepted_upper.accepted_sequence==5&&page->evicted_through==0&&!page->gap);
    require(records[2].position==proof_.accepted);
    require(std::string_view(records[2].text.data(),records[2].text_size)=="ock.log/1 level=info component=host event=diagnostic count=count:1");
    log_upper_=page->accepted_upper.accepted_sequence;
    auto session=runtime.open({{std::byte{7}}},{rules(),o.auth->deadline,false});require(bool(session));o.session.emplace(std::move(*session));
    auto caller=o.session->verify({principal(),{},{}});require(bool(caller));o.caller=*caller;
    auto computing=o.session->bind<Value,Value>(key("native.compute"),{},Shape::Read,o.caller,std::array{target()},targets,name("consumer.compute"));
    auto reading=o.session->bind<Value,Value>(key("native.read"),{},Shape::Read,o.caller,std::array{target()},targets,name("consumer.read"));
    require(bool(computing)&&bool(reading));o.computing.emplace(std::move(*computing));o.reading.emplace(std::move(*reading));
  }
  void prove_invalid(){
    auto& o=*owners_;auto before=entered;
    auto invalid=o.computing->invoke(Value{-1},{{},o.clock->now()+std::chrono::seconds(10),100});
    require(std::holds_alternative<Rejected>(invalid)&&entered==before);
  }
  void call(unsigned index){
    auto& o=*owners_;
    InvokeOptions options{{},o.clock->now()+std::chrono::seconds(10),100};
    auto reply=(index%2?o.reading:o.computing)->invoke(Value{5},options);
    require(result(reply)==(index%2?12:7));
  }
  void shutdown(){
    auto& o=*owners_;std::array<PublicLogRecord,64> records{};
    auto page=o.runtime->copy_logs({o.runtime->incarnation(),1,0},records);
    require(bool(page)&&page->accepted_upper.accepted_sequence==log_upper_&&entered==46);
    auto stopped=o.runtime->shutdown_until(o.clock->now()+std::chrono::seconds(2));
    require(stopped.quiescent&&stopped.phase==host::HostPhase::Stopped&&proof_.stops==1);
    auto reply=o.computing->invoke(Value{5},{{},o.clock->now()+std::chrono::seconds(10),100});
    require(std::holds_alternative<Rejected>(reply)&&entered==46);
  }
  void release_bound(){owners_->computing.reset();owners_->reading.reset();require(!owners_->computing&&!owners_->reading);}
  void release_session(){require(bool(owners_->session->close()));owners_->session.reset();owners_->caller.reset();}
  void release_owners(){
    require(!lifecycle_.expired()&&!reader_.expired()&&!target_.expired());
    owners_.reset();require(lifecycle_.expired()&&reader_.expired()&&target_.expired()&&proof_.destroyed==1);
  }
  unsigned sentinels()const{return proof_.destroyed;}
  unsigned owner_mask()const{return !owners_?0:((owners_->computing||owners_->reading?1:0)|(owners_->session?2:0)|(owners_->runtime?4:0)|8);}
};
}
