#include "tests/contract/native/fixtures.hpp"
#include "packages/runtime/executions/invocation_access.hpp"
#include <ock/state/provider.hpp>
#include <ock/runtime/atomic.hpp>
#include <semaphore>
#ifdef OCK_STATE_HOST_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/runtime/host.hpp>
#endif

using namespace ock::contracts;using namespace ock::runtime;
struct StateItem {int value;};
template<> struct ock::state::RootContract<StateItem>{static Result<StateItem> freeze(const StateItem& v){return v;}static Result<std::size_t> bytes(const StateItem&){return sizeof(StateItem);}};
template<> struct ock::contracts::TypeContract<StateItem>{static TypeIdentity identity(){return{name("runtime.state.item"),ver(),{}};}static Result<void> validate(const StateItem&){return{};}};
static AtomicDomainRef state_domain(){return{ProviderContract<ock::state::ObjectStateProvider>::key(),policy_test::target(),id<foundation::RegistryGeneration>()};}
struct StateReads final:ock::state::SnapshotAuthority {
  std::shared_ptr<CallerAuthorityPort> callers;explicit StateReads(std::shared_ptr<CallerAuthorityPort> v):callers(std::move(v)){}
  Result<void> authorize(const CallerView& caller,const AtomicDomainRef& domain)const override {
    if(domain!=state_domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*callers,caller);}
};
struct CandidateReader {};
static OperationKey read_key(){return {name("state.candidate-read"),ver()};}
static OperationKey compute_key(){return {name("state.compute"),ver()};}
static Result<int> ordinary_read(const int&,WorkContext&,ReadServices<CandidateReader>&){return 0;}
static Result<int> candidate_read(const int&,const ock::state::ObjectStateProvider::CandidateReadPort& view,WorkContext&) {
  auto item=view.find(policy_test::target(2));if(!item)return make_unexpected(error(ContractsErrc::Rejected));return item->value().get<StateItem>()->value;
}
static Result<int> candidate_compute(const int& value,WorkContext&){return value+10;}
static std::atomic<unsigned> state_entries=0;
static std::function<void(WorkContext&)> before_return;
static bool require_resource=false;
static registry::RevisionPolicy revision_policy=registry::RevisionPolicy::ServerCapture;
static Result<int> state_edit(const int& input,EditView<ock::state::ObjectStateProvider>& view,WorkContext& work) {
  if(require_resource)CHECK(work.granted_resources().size()==1);
  ++state_entries;auto value=ock::state::ObjectValue::freeze(StateItem{input},1024);if(!value)return make_unexpected(value.error());
  auto record=ock::state::ObjectRecord::create(policy_test::target(2),*value,{},4);if(!record)return make_unexpected(record.error());
  auto result=view.edit().find(record->id())?view.edit().replace(*record):view.edit().create(*record);
  if(!result)return make_unexpected(result.error());if(before_return)before_return(work);return input+1;
}
struct Executor final:ExecutorPort{Result<void> submit(std::unique_ptr<ReadyWork>)override{return reject(ContractsErrc::Rejected);}};
static Result<std::size_t> state_target(const int&,std::span<foundation::ObjectId> out)noexcept {
  if(out.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));out[0]=policy_test::target();return 1;
}
static Result<std::size_t> input_bytes(const int&){return sizeof(int);}
static std::atomic<bool> reject_reply_bytes=false,reject_result_bytes=false;
static Result<std::size_t> reply_bytes(const InvokeReply<int>&){
  if(reject_reply_bytes.load())return make_unexpected(error(ContractsErrc::BudgetExceeded));return 512;}
static Result<std::size_t> result_bytes(const int&){
  if(reject_result_bytes.load())return make_unexpected(error(ContractsErrc::BudgetExceeded));return sizeof(int);}
int main() try {
  auto configuration=policy_test::configuration();configuration.operations[1].permits_group=true;
  auto all_rules=policy_test::rules();
  for(const auto& rule:policy_test::rules())if(rule.operation==policy_test::operation()) {
    auto read=rule;read.operation=policy::OperationSelector{read_key(),{}};all_rules.push_back(read);
    auto compute=rule;compute.operation=policy::OperationSelector{compute_key(),{}};all_rules.push_back(compute);
  }
  for(auto& principal:configuration.principals)principal.rules=all_rules;
  for(auto& operation:configuration.operations)operation.module_rules=all_rules;
  for(auto& use:configuration.uses)use.module_rules=all_rules;
  for(auto& target:configuration.targets)target.rules=all_rules;
  configuration.operations.push_back({{read_key(),{}},{name("allow")},all_rules,false});
  configuration.operations.push_back({{compute_key(),{}},{name("allow")},all_rules,false});
  policy_test::Env policy({},configuration);policy.auth->identity.ceiling.rules=all_rules;
  auto full_session=policy.assembly.store->open({{std::byte{7}}},{all_rules,policy.auth->identity.deadline,false});CHECK(full_session);policy.session=*full_session;
  auto full_caller=policy.session->verify({policy_test::principal(),{},{}});CHECK(full_caller);policy.caller=*full_caller;
  ock::state::DomainOptions domain_options{.root_bytes=1024*1024,.candidate_bytes=1024*1024,.result_bytes=4096,
    .history_entries=8,.history_bytes=65536,.snapshot_pins=16,.history_pins=4,.inflight_commits=1,.reclaim_batch=2};
  auto reads=std::make_shared<StateReads>(policy.session->callers());
  auto state=ock::state::StateDomain<ock::state::ObjectRoot>::create(state_domain(),{},domain_options,
      reads);CHECK(state);
  auto provider=ock::state::ObjectMemoryProvider::create(*state,{1024*1024,1024*1024,128});CHECK(provider);
  registry::ModuleManifest manifest{name("state.runtime"),ver()};
  manifest.operations={key(),policy_test::operation(2).operation,read_key(),compute_key()};manifest.providers={ProviderContract<ock::state::ObjectStateProvider>::key()};manifest.executors={name("inline")};
  manifest.required_providers.push_back({{name("state.runtime"),ProviderContract<ock::state::ObjectStateProvider>::key()},CppTypeToken::of<ock::state::ObjectStateProvider>()});
  registry::ModuleInput module{manifest,{}, {},{},{},{{name("inline"),name("app"),false,false,std::make_shared<Executor>()}}, {}};
  module.manifest.services.push_back(name("candidate.reader"));
  module.manifest.required_services.push_back({{name("state.runtime"),name("candidate.reader")},CppTypeToken::of<CandidateReader>()});
  module.services.push_back(*registry::ServiceBinding::make(name("candidate.reader"),std::make_shared<CandidateReader>()));
  auto binding=registry::ProviderBinding::make<ock::state::ObjectStateProvider>(*provider);CHECK(binding);module.providers.push_back(*binding);
  module.register_operations=[](registry::Registrar& registrar){
    DefinitionInput definition{key(),{}, {true,false,false,name("inline"),name("app")},AtomicMode::StateEdit,{name("allow")},"Runtime StateEdit"};
    registry::OperationOptions options{{},{registry::ProviderRef{name("state.runtime"),ProviderContract<ock::state::ObjectStateProvider>::key()}},
      {name("state.runtime"),name("inline")},{},false};
    options.revision_policy=revision_policy;
    if(require_resource)options.resources={{name("state.runtime"),name("atomic.resource")}};
    registry::SubmissionStorage<int,int> storage{64,2048,input_bytes,reply_bytes,result_bytes};
    CHECK(registrar.state_edit(state_edit,definition,options,storage));
    definition.key=policy_test::operation(2).operation;
    CHECK(atomic::register_group<ock::state::ObjectStateProvider>(registrar,definition,options));
    definition.key=read_key();definition.atomic_mode=AtomicMode::Incompatible;
    options.read_service=registry::ServiceRef{name("state.runtime"),name("candidate.reader")};
    CHECK((registrar.candidate_read<int,int,CandidateReader,ock::state::ObjectStateProvider>(ordinary_read,candidate_read,definition,options,storage)));
    definition.key=compute_key();definition.atomic_mode=AtomicMode::PureCompute;options.provider.reset();options.read_service.reset();
    CHECK(registrar.compute(candidate_compute,definition,options,storage));
  };
  auto batch=registry::RegistrationBatch::create({});CHECK(batch);CHECK((*batch)->add(module));auto catalog=(*batch)->publish();CHECK(catalog);
  auto candidate=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(*catalog,key(),{},AtomicMode::StateEdit,33,state_domain(),std::array{policy_test::target()},state_target);CHECK(candidate);
  auto forged_digest=ContractDigest{};forged_digest.bytes[0]=std::byte{1};
  CHECK((!registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(*catalog,key(),{},AtomicMode::StateEdit,33,state_domain(),std::array{policy_test::target(2)},state_target)));
  CHECK((!registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(*catalog,key(),forged_digest,AtomicMode::StateEdit,33,state_domain(),std::array{policy_test::target()},state_target)));
  auto candidate_frame=(*provider)->begin(state_domain(),policy.caller->view());CHECK(candidate_frame);
  auto candidate_budget=*foundation::CheckedCount<std::uint64_t>::create(0,100);
  WorkContext candidate_work({},policy.clock->now()+std::chrono::seconds(5),candidate_budget,name("candidate"),{});
  auto candidate_result=candidate->port()->invoke((*candidate_frame)->edit,candidate_work);CHECK(candidate_result&&candidate_result->get<int>()&&*candidate_result->get<int>()==34);
  CHECK((*state)->snapshot(policy.caller->view())->revision()==0);state_entries=0;
  // Hold C1's real provider report until C2 has published and sealed its result.
  struct InterleavingProvider final:InlineAtomicProviderPort<ock::state::ObjectStateProvider>,PublicationAuthorityPort {
    std::shared_ptr<ock::state::ObjectMemoryProvider> delegate;
    std::atomic<bool> delay{true},timed_out{false};std::binary_semaphore entered{0},release{0};
    Result<std::unique_ptr<ock::state::ObjectStateProvider::Frame>> begin(const AtomicDomainRef& d,const CallerView& c)override{return delegate->begin(d,c);}
    Result<AtomicDomainRef> resolve(foundation::ObjectId target)const override{return delegate->resolve(target);}
    Result<PreparedBase> base(const ock::state::ObjectStateProvider::Frame& frame)const override{return delegate->base(frame);}
    Result<std::shared_ptr<const PreparedCommit>> prepare(ock::state::ObjectStateProvider::Frame& frame,const PreparedIdentity& identity)override{return delegate->prepare(frame,identity);}
    Result<void> commit(std::shared_ptr<const PreparedCommit> prepared,std::shared_ptr<const ActionPermit> permit,std::shared_ptr<PermitAuthorityPort> authority,PermitBinding binding,std::shared_ptr<CommitReceiver> receiver)override{return delegate->commit(prepared,permit,authority,binding,receiver);}
    Result<CommitReport> commit_inline(std::shared_ptr<const PreparedCommit> prepared,std::shared_ptr<const ActionPermit> permit,std::shared_ptr<PermitAuthorityPort> authority,PermitBinding binding)noexcept override{
      auto report=delegate->commit_inline(prepared,permit,authority,binding);
      if(delay.exchange(false)){entered.release();if(!release.try_acquire_for(std::chrono::seconds(3)))timed_out=true;}
      return report;
    }
    Result<std::shared_ptr<const PublicationProof>> attest(const PublishedCommit& value)override{return delegate->attest(value);}
    Result<void> validate(const PublicationProof& proof,const PublishedCommit& value)const override{return delegate->validate(proof,value);}
  };
  {
    auto interleaved_state=ock::state::StateDomain<ock::state::ObjectRoot>::create(state_domain(),{},domain_options,reads);CHECK(interleaved_state);
    auto delegate=ock::state::ObjectMemoryProvider::create(*interleaved_state,{1024*1024,1024*1024,128});CHECK(delegate);
    auto interleaver=std::make_shared<InterleavingProvider>();interleaver->delegate=*delegate;
    auto other_module=module;other_module.providers={*registry::ProviderBinding::make<ock::state::ObjectStateProvider>(interleaver)};
    auto other_batch=registry::RegistrationBatch::create({});CHECK(other_batch);CHECK((*other_batch)->add(other_module));auto other_catalog=(*other_batch)->publish();CHECK(other_catalog);
    auto concurrent_threads=std::make_shared<native_test::Threads>();concurrent_threads->any_thread=true;
    invocation::NativeBudget concurrent_budget;concurrent_budget.concurrent_calls_per_binding=2;
    auto other_engine=invocation::NativeEngine::create(*other_catalog,policy.session,concurrent_threads,concurrent_budget);CHECK(other_engine);
    auto other_bound=(*other_engine)->bind<int,int>(key(),{},Shape::StateEdit,policy.caller,std::array{policy_test::target()},state_target,name("state.interleaved"));CHECK(other_bound);
    invocation::InvokeOptions concurrent_options{{},policy.clock->now()+std::chrono::seconds(5),100};
    std::optional<InvokeReply<int>> first;
    std::jthread invoking([&]{first.emplace(other_bound->invoke(101,concurrent_options));});
    CHECK(interleaver->entered.try_acquire_for(std::chrono::seconds(2)));
    auto second=other_bound->invoke(102,concurrent_options);interleaver->release.release();invoking.join();
    CHECK(!interleaver->timed_out&&first&&std::holds_alternative<Completed<int>>(*first)&&std::holds_alternative<Completed<int>>(second));
    auto& first_commit=std::get<StateCommitted<int>>(std::get<Completed<int>>(*first).outcome.value());
    auto& second_commit=std::get<StateCommitted<int>>(std::get<Completed<int>>(second).outcome.value());
    CHECK(first_commit.revision==1&&*first_commit.result==102&&second_commit.revision==2&&*second_commit.result==103);
    CHECK((*interleaved_state)->snapshot(policy.caller->view())->revision()==2);
  }
  state_entries=0;
  auto threads=std::make_shared<native_test::Threads>();auto engine=invocation::NativeEngine::create(*catalog,policy.session,threads,{});CHECK(engine);
  auto bound=(*engine)->bind<int,int>(key(),{},Shape::StateEdit,policy.caller,std::array{policy_test::target()},state_target,name("state.native"));CHECK(bound);
  invocation::InvokeOptions options{{},policy.clock->now()+std::chrono::seconds(5),100};
  auto reply=bound->invoke(41,options);CHECK(std::holds_alternative<Completed<int>>(reply));
  const auto& outcome=std::get<Completed<int>>(reply).outcome;CHECK(std::holds_alternative<StateCommitted<int>>(outcome.value()));
  const auto& committed=std::get<StateCommitted<int>>(outcome.value());CHECK(committed.result&&*committed.result==42&&committed.revision==1);
  CHECK(outcome.facts().values().size()==2&&state_entries==1);
  auto snapshot=(*state)->snapshot(policy.caller->view());CHECK(snapshot&&snapshot->revision()==1);
  auto record=snapshot->value().find(policy_test::target(2));CHECK(record&&record->value().get<StateItem>()->value==41);
  reject_result_bytes=true;auto rejected=bound->invoke(51,options);reject_result_bytes=false;
  CHECK(std::holds_alternative<Completed<int>>(rejected));
  CHECK(std::holds_alternative<FailedBeforeApply>(std::get<Completed<int>>(rejected).outcome.value()));
  auto unchanged=(*state)->snapshot(policy.caller->view());CHECK(unchanged&&unchanged->revision()==1&&state_entries==2);
  auto managed=executions::detail::InvocationAccess::registered_record(*bound,52,options);CHECK(managed);
  threads->role=invocation::ThreadRole::Worker;
  reject_reply_bytes=true;
  CHECK((*managed)->run_once({}));CHECK((*managed)->reply());
  reject_reply_bytes=false;
  const auto& managed_reply=*(*managed)->reply();CHECK(std::holds_alternative<Completed<int>>(managed_reply));
  const auto& managed_outcome=std::get<Completed<int>>(managed_reply).outcome;
  if(!std::holds_alternative<StateCommitted<int>>(managed_outcome.value()))
    std::visit([&](const auto& value){using V=std::decay_t<decltype(value)>;
      std::cerr<<"managed outcome index="<<managed_outcome.value().index()<<" facts="<<managed_outcome.facts().values().size();
      if constexpr(std::same_as<V,FailedBeforeApply>||std::same_as<V,CancelledBeforeApply>)std::cerr<<" error="<<value.reason.code().value();
      std::cerr<<'\n';},managed_outcome.value());
  CHECK(std::holds_alternative<StateCommitted<int>>(managed_outcome.value()));
  CHECK(*std::get<StateCommitted<int>>(managed_outcome.value()).result==53);
  CHECK((*managed)->reply_bytes()==2048);
  auto managed_snapshot=(*state)->snapshot(policy.caller->view());CHECK(managed_snapshot&&managed_snapshot->revision()==2);
  CHECK(managed_snapshot->value().find(policy_test::target(2))->value().get<StateItem>()->value==52&&state_entries==3);
  // Real Policy cancellation after business entry must still lose before commit claim.
  threads->role=invocation::ThreadRole::Application;
  std::stop_source cancellation;
  auto cancelled_options=options;cancelled_options.stop=cancellation.get_token();
  before_return=[&](WorkContext&){cancellation.request_stop();};
  auto cancelled=bound->invoke(60,cancelled_options);before_return={};
  CHECK(std::holds_alternative<Completed<int>>(cancelled));
  CHECK(!std::holds_alternative<StateCommitted<int>>(std::get<Completed<int>>(cancelled).outcome.value()));
  CHECK((*state)->snapshot(policy.caller->view())->revision()==2);
  auto explicit_options=options;explicit_options.expected_state=PreparedBase{1,1};
  auto entries=state_entries.load();auto stale=bound->invoke(61,explicit_options);
  CHECK(state_entries==entries&&std::holds_alternative<Completed<int>>(stale));
  CHECK(!std::holds_alternative<StateCommitted<int>>(std::get<Completed<int>>(stale).outcome.value()));
  using GroupInput=atomic::Input<ock::state::ObjectStateProvider>;
  auto linked=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(*catalog,key(),{},AtomicMode::StateEdit,33,state_domain(),std::array{policy_test::target()},state_target,0);CHECK(linked);
  CHECK(!GroupInput::create({*linked}));
  CHECK(GroupInput::create(std::vector<registry::CandidateStep<ock::state::ObjectStateProvider>>(128,*candidate)));
  CHECK(!GroupInput::create(std::vector<registry::CandidateStep<ock::state::ObjectStateProvider>>(129,*candidate)));
  auto read_step=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int,CandidateReader>(*catalog,read_key(),{},AtomicMode::CandidateRead,0,state_domain(),std::array{policy_test::target()},state_target);CHECK(read_step);
  auto compute_step=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(*catalog,compute_key(),{},AtomicMode::PureCompute,0,state_domain(),std::array{policy_test::target()},state_target,2);CHECK(compute_step);
  auto group=GroupInput::create({*candidate,*linked,*read_step,*compute_step},{atomic::Assertion::equals(1,35),atomic::Assertion::equals(2,34),atomic::Assertion::equals(3,44)});CHECK(group);
  auto nested=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,GroupInput,atomic::Results>(*catalog,policy_test::operation(2).operation,{},AtomicMode::StateEdit,*group,state_domain(),std::array{policy_test::target()},atomic::target<ock::state::ObjectStateProvider>);CHECK(!nested);
  auto group_bound=(*engine)->bind<GroupInput,atomic::Results>(policy_test::operation(2).operation,{},Shape::StateEdit,policy.caller,
      std::array{policy_test::target()},atomic::target<ock::state::ObjectStateProvider>,name("atomic.native"));CHECK(group_bound);
  auto grouped=group_bound->invoke(*group,options);CHECK(std::holds_alternative<Completed<atomic::Results>>(grouped));
  auto& group_outcome=std::get<Completed<atomic::Results>>(grouped).outcome;
  CHECK(std::holds_alternative<StateCommitted<atomic::Results>>(group_outcome.value()));
  CHECK((*state)->snapshot(policy.caller->view())->revision()==3);
  auto rejected_group=GroupInput::create({*candidate,*candidate},{atomic::Assertion::equals(1,999)});CHECK(rejected_group);
  auto not_published=group_bound->invoke(*rejected_group,options);CHECK(std::holds_alternative<Completed<atomic::Results>>(not_published));
  CHECK(!std::holds_alternative<StateCommitted<atomic::Results>>(std::get<Completed<atomic::Results>>(not_published).outcome.value()));
  CHECK((*state)->snapshot(policy.caller->view())->revision()==3);entries=state_entries.load();
  before_return=[&](WorkContext&){CHECK(policy.assembly.administration->replace_principal_policy({policy_test::principal(),{}}));};
  auto revoked=bound->invoke(63,options);before_return={};
  CHECK(std::holds_alternative<Completed<int>>(revoked));
  CHECK(!std::holds_alternative<StateCommitted<int>>(std::get<Completed<int>>(revoked).outcome.value()));
  CHECK(policy.assembly.administration->replace_principal_policy({policy_test::principal(),all_rules}));
  before_return=[&](WorkContext&){policy.clock->elapsed.fetch_add(6000);};
  auto expired=bound->invoke(64,options);before_return={};
  CHECK(std::holds_alternative<Completed<int>>(expired));
  CHECK(!std::holds_alternative<StateCommitted<int>>(std::get<Completed<int>>(expired).outcome.value()));
  CHECK((*state)->snapshot(policy.caller->view())->revision()==3);entries=state_entries.load();
  options.deadline=policy.clock->now()+std::chrono::seconds(5);
  struct BoundaryAuthority final:PermitAuthorityPort {
    std::shared_ptr<policy::ActionAuthorization> action;std::function<void()> before,after;
    Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant& caller,const PermitBinding& binding)override{return action->issue(caller,binding);}
    Result<void> consume(const ActionPermit& permit,const PermitBinding& binding)override{return action->consume(permit,binding);}
    Result<void> consume_claimed(const ActionPermit& permit,const PermitBinding& binding,CommitClaim& claim)override{
      if(before)before();auto result=action->consume_claimed(permit,binding,claim);if(result&&after)after();return result;
    }
  };
  for(unsigned factor=0;factor<4;++factor)for(bool close_before:{true,false}) {
    auto boundary=ock::state::StateDomain<ock::state::ObjectRoot>::create(state_domain(),{},domain_options,reads);CHECK(boundary);
    auto base=(*boundary)->snapshot(policy.caller->view());CHECK(base);
    PreparedIdentity identity{id<CommitId>(20+close_before),id<ReservationId>(20+close_before),state_domain(),0,1};
    auto prepared=(*boundary)->prepare(*base,base->value(),identity,0);CHECK(prepared);
    auto action=policy.session->prepare(*policy.caller,{policy_test::operation(),policy_test::target(),{{policy_test::operation(),{policy_test::target()}}},options.deadline});CHECK(action);
    auto permit=(*action)->issue();CHECK(permit);auto binding=(*action)->current_expected_binding();CHECK(binding);
    BoundaryAuthority authority;authority.action=*action;
    auto invalidate=[&]{
      if(factor==0)(*boundary)->close();
      if(factor==1)(void)(*action)->cancel();
      if(factor==2)CHECK(policy.assembly.administration->replace_principal_policy({policy_test::principal(),{}}));
      if(factor==3)policy.clock->elapsed.fetch_add(6000);
    };
    if(close_before)authority.before=invalidate;else authority.after=invalidate;
    auto result=(*boundary)->commit(*prepared,*permit,authority,*binding);
    CHECK(bool(result)!=close_before);
    if(result)CHECK((*boundary)->validate(*result->proof,result->publication));
    if(factor==0)CHECK(!(*boundary)->snapshot(policy.caller->view()));
    else CHECK((*boundary)->snapshot(policy.caller->view())->revision()==(result?1:0));
    if(factor==2)CHECK(policy.assembly.administration->replace_principal_policy({policy_test::principal(),all_rules}));
    options.deadline=policy.clock->now()+std::chrono::seconds(5);
  }


#ifdef OCK_STATE_HOST_TESTS
  struct Recorder final:RequiredRecordPort {
    std::atomic<bool> hold=false;std::binary_semaphore reported{0};std::mutex mutex;
    std::shared_ptr<RecordReceiver> retained;
    Result<std::unique_ptr<RecordReservation>> reserve(const RecordRequest&)override{return std::unique_ptr<RecordReservation>(new RecordReservation);}
    Result<void> record(std::unique_ptr<RecordReservation>,std::shared_ptr<const RecordRequestSnapshot> request,std::shared_ptr<RecordReceiver> receiver)override{
      auto input=request->value();
      if(!hold){receiver->completed({input.execution,input.commit,input.effect,RequiredRecordState::Recorded,{}});return {};}
      {std::lock_guard lock(mutex);retained=receiver;}
      receiver->completed({input.execution,input.commit,input.effect,RequiredRecordState::Failed,RecordFailure{error(ContractsErrc::Rejected),true,RepairKind::ManualReview}});
      reported.release();return make_unexpected(error(ContractsErrc::Rejected));
    }
    void release(){std::shared_ptr<RecordReceiver> old;{std::lock_guard lock(mutex);old=std::move(retained);}}
  };
  auto recorder=std::make_shared<Recorder>();
  struct Factory final:host::HostExecutionFactoryPort {
    std::shared_ptr<RequiredRecordPort> recorder;
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      auto pool=ock::cpu_pool::Executor::create({2,2});if(!pool)return make_unexpected(pool.error());
      std::shared_ptr<ExecutorControlPort> executor=std::move(*pool);
      host::ExecutionOptions config;config.required_record=recorder;config.subjects={{policy_test::principal().principal_id}};
      config.slots={{"atomic.slot",1,false}};
      config.resources={{{name("state.runtime"),name("atomic.resource")},{{"atomic.slot",resources::Mode::Exclusive,1}}}};
      auto value=host::make_executions(id,executor,config);
      if(!value)(void)executor->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));
      return value;
    }
  };
  struct Lifecycle final:host::ModuleLifecyclePort {
    Result<void> start(const host::ModuleContext&) override{return {};}
    host::ModuleStopResult stop() override{return {true,{}};}
  };
  struct HostThreads final:invocation::TrustedThreadPort {
    const std::thread::id owner=std::this_thread::get_id();
    Result<invocation::ThreadObservation> current() const noexcept override {
      return invocation::ThreadObservation{std::this_thread::get_id()==owner?invocation::ThreadRole::Application:invocation::ThreadRole::Worker,name("app"),true};
    }
  };
  host::HostOptions host_options;host_options.enable_state=true;host_options.native.concurrent_calls_per_binding=2;
  auto factory=std::make_shared<Factory>();factory->recorder=recorder;
  auto host=host::NativeHost::create(host_options,configuration,{policy.auth,policy.clock,policy.digest,std::make_shared<HostThreads>(),{},factory});CHECK(host);
  struct Shutdown {host::NativeHost& owner;~Shutdown(){(void)owner.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));}} shutdown{**host};
  revision_policy=registry::RevisionPolicy::RequireExplicitRevision;require_resource=true;
  module.manifest.resources.push_back(name("atomic.resource"));
  module.manifest.required_resources.push_back({name("state.runtime"),name("atomic.resource")});
  module.resources.push_back({name("atomic.resource"),std::make_shared<ResourceLease>()});
  CHECK((*host)->add({module,std::make_shared<Lifecycle>()}));CHECK((*host)->start());
  auto session=(*host)->open({{std::byte{7}}},{all_rules,policy.auth->identity.deadline,false});CHECK(session);
  auto context=session->catalog_context();CHECK(context);reads->callers=context->authorization->callers();
  auto caller=session->verify({policy_test::principal(),{},{}});CHECK(caller);
  auto hosted=session->bind<int,int>(key(),{},Shape::StateEdit,*caller,std::array{policy_test::target()},state_target,name("state.host"));CHECK(hosted);
  auto missing=hosted->submit(70,options);CHECK(std::holds_alternative<Accepted>(missing));
  auto missing_ref=std::get<Accepted>(missing).execution;
  auto missing_wait=session->wait(**caller,missing_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(missing_wait&&missing_wait->state==host::ExecutionWaitState::Terminal);
  auto missing_result=session->result<int>(**caller,missing_ref);CHECK(missing_result);
  CHECK(std::holds_alternative<Completed<int>>(*missing_result->value));
  CHECK(!std::holds_alternative<StateCommitted<int>>(std::get<Completed<int>>(*missing_result->value).outcome.value()));
  CHECK(state_entries==entries);
  auto submit_options=options;submit_options.expected_state=PreparedBase{3,1};
  auto submitted=hosted->submit(71,submit_options);CHECK(std::holds_alternative<Accepted>(submitted));
  auto reference=std::get<Accepted>(submitted).execution;
  auto waited=session->wait(**caller,reference,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
  auto result=session->result<int>(**caller,reference);CHECK(result);
  CHECK(std::holds_alternative<Completed<int>>(*result->value));
  const auto& published=std::get<StateCommitted<int>>(std::get<Completed<int>>(*result->value).outcome.value());
  CHECK(published.revision==4&&published.result&&*published.result==72);
  CHECK((*state)->snapshot((*caller)->view())->revision()==4);
  auto host_context=session->catalog_context();CHECK(host_context);
  auto host_catalog=std::dynamic_pointer_cast<const registry::Catalog>(host_context->definitions);CHECK(host_catalog);
  auto host_candidate=registry::CandidateBindings::bind<ock::state::ObjectStateProvider,int,int>(host_catalog,key(),{},AtomicMode::StateEdit,80,state_domain(),std::array{policy_test::target()},state_target);CHECK(host_candidate);
  auto host_group=GroupInput::create({*host_candidate,*host_candidate},{atomic::Assertion::equals(1,81)});CHECK(host_group);
  auto host_group_bound=session->bind<GroupInput,atomic::Results>(policy_test::operation(2).operation,{},Shape::StateEdit,*caller,
      std::array{policy_test::target()},atomic::target<ock::state::ObjectStateProvider>,name("atomic.host"));CHECK(host_group_bound);
  submit_options.expected_state=PreparedBase{4,1};
  auto wrong_registry=host_group_bound->invoke(*group,submit_options);CHECK(std::holds_alternative<Rejected>(wrong_registry));
  auto group_submit=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(group_submit));
  auto group_ref=std::get<Accepted>(group_submit).execution;
  auto group_wait=session->wait(**caller,group_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(group_wait&&group_wait->state==host::ExecutionWaitState::Terminal);
  auto group_result=session->result<atomic::Results>(**caller,group_ref);CHECK(group_result);
  CHECK(std::holds_alternative<Completed<atomic::Results>>(*group_result->value));
  const auto& group_commit=std::get<StateCommitted<atomic::Results>>(std::get<Completed<atomic::Results>>(*group_result->value).outcome.value());
  CHECK(group_commit.revision==5&&group_commit.result&&group_commit.result->values.size()==2);
  CHECK(*group_commit.result->values[1].get<int>()==81);
  auto entered_step=std::make_shared<std::binary_semaphore>(0),release_step=std::make_shared<std::binary_semaphore>(0);
  auto barrier_calls=std::make_shared<std::atomic<unsigned>>(0);
  before_return=[entered_step,release_step,barrier_calls](WorkContext&){if(barrier_calls->fetch_add(1)==0){entered_step->release();CHECK(release_step->try_acquire_for(std::chrono::seconds(3)));}};
  submit_options.expected_state=PreparedBase{5,1};
  auto first_group=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(first_group));
  CHECK(entered_step->try_acquire_for(std::chrono::seconds(2)));
  auto second_group=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(second_group));
  auto second_ref=std::get<Accepted>(second_group).execution;
  auto waiting=session->wait(**caller,second_ref,std::chrono::steady_clock::now()+std::chrono::milliseconds(30));
  CHECK(waiting&&waiting->state==host::ExecutionWaitState::Timeout&&*barrier_calls==1);
  release_step->release();
  auto first_wait=session->wait(**caller,std::get<Accepted>(first_group).execution,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(first_wait&&first_wait->state==host::ExecutionWaitState::Terminal);
  auto second_wait=session->wait(**caller,second_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(second_wait&&second_wait->state==host::ExecutionWaitState::Terminal);
  before_return={};CHECK(*barrier_calls==2);
  CHECK((*state)->snapshot((*caller)->view())->revision()==6);
  auto stale_group=session->result<atomic::Results>(**caller,second_ref);CHECK(stale_group);
  CHECK(!std::holds_alternative<StateCommitted<atomic::Results>>(std::get<Completed<atomic::Results>>(*stale_group->value).outcome.value()));
  auto restricted_rules=all_rules;
  restricted_rules.erase(std::remove_if(restricted_rules.begin(),restricted_rules.end(),[](const auto& rule){return rule.operation!=policy_test::operation(2);}),restricted_rules.end());
  auto restricted=(*host)->open({{std::byte{7}}},{restricted_rules,policy.auth->identity.deadline,false});CHECK(restricted);
  auto other_caller=restricted->verify({policy_test::principal(),{},{}});CHECK(other_caller);
  auto other_bound=restricted->bind<GroupInput,atomic::Results>(policy_test::operation(2).operation,{},Shape::StateEdit,*other_caller,
      std::array{policy_test::target()},atomic::target<ock::state::ObjectStateProvider>,name("atomic.restricted"));CHECK(other_bound);
  submit_options.expected_state=PreparedBase{6,1};auto before_denied=state_entries.load();
  auto denied_submit=other_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(denied_submit));
  auto denied_ref=std::get<Accepted>(denied_submit).execution;
  auto denied_wait=restricted->wait(**other_caller,denied_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(denied_wait&&denied_wait->state==host::ExecutionWaitState::Terminal);
  auto denied_result=restricted->result<atomic::Results>(**other_caller,denied_ref);CHECK(denied_result);
  CHECK(std::holds_alternative<Completed<atomic::Results>>(*denied_result->value));
  CHECK(!std::holds_alternative<StateCommitted<atomic::Results>>(std::get<Completed<atomic::Results>>(*denied_result->value).outcome.value()));
  CHECK(state_entries==before_denied);
  entered_step=std::make_shared<std::binary_semaphore>(0);release_step=std::make_shared<std::binary_semaphore>(0);
  before_return=[entered_step,release_step](WorkContext&){entered_step->release();CHECK(release_step->try_acquire_for(std::chrono::seconds(3)));};
  auto cancel_submit=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(cancel_submit));
  auto cancel_ref=std::get<Accepted>(cancel_submit).execution;CHECK(entered_step->try_acquire_for(std::chrono::seconds(2)));
  CHECK(session->cancel(**caller,cancel_ref));release_step->release();
  auto cancel_wait=session->wait(**caller,cancel_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(cancel_wait&&cancel_wait->state==host::ExecutionWaitState::Terminal);
  before_return={};auto cancelled_group=session->result<atomic::Results>(**caller,cancel_ref);CHECK(cancelled_group);
  CHECK(!std::holds_alternative<StateCommitted<atomic::Results>>(std::get<Completed<atomic::Results>>(*cancelled_group->value).outcome.value()));
  CHECK((*state)->snapshot((*caller)->view())->revision()==6);
  recorder->hold=true;
  auto record_failure=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(record_failure));
  auto record_ref=std::get<Accepted>(record_failure).execution;CHECK(recorder->reported.try_acquire_for(std::chrono::seconds(2)));
  CHECK((*state)->snapshot((*caller)->view())->revision()==7);
  auto not_drained=session->wait(**caller,record_ref,std::chrono::steady_clock::now()+std::chrono::milliseconds(20));CHECK(not_drained&&not_drained->state==host::ExecutionWaitState::Timeout);
  CHECK(!session->result<atomic::Results>(**caller,record_ref));
  recorder->release();
  auto drained=session->wait(**caller,record_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));CHECK(drained&&drained->state==host::ExecutionWaitState::Terminal);
  auto preserved=session->result<atomic::Results>(**caller,record_ref);CHECK(preserved);
  const auto& preserved_outcome=std::get<Completed<atomic::Results>>(*preserved->value).outcome;
  CHECK(std::holds_alternative<StateCommitted<atomic::Results>>(preserved_outcome.value()));
  CHECK(preserved_outcome.evidence()==EvidenceState::RequiredRecordFailed&&preserved_outcome.facts().values().size()==2);
  CHECK((*host)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3)).quiescent);



#endif
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
