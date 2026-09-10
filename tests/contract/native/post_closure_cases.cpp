#include "fixtures.hpp"
#include "packages/runtime/executions/managed_execution.hpp"
#include <functional>

namespace native_test::closure {
struct Input {std::shared_ptr<const std::vector<int>> values;};
struct Value {int value;};
}
namespace ock::contracts {
template<> struct TypeContract<native_test::closure::Input> {
  static TypeIdentity identity(){return {*Name::parse("closure.input"),*OperationVersion::parse("1.0.0",5),{}};}
  static constexpr auto async_ownership=AsyncOwnership::Owning;
  static Result<void> validate(const native_test::closure::Input& v){return v.values?Result<void>{}:reject(ContractsErrc::InvalidFact);}
};
template<> struct TypeContract<native_test::closure::Value> {
  static TypeIdentity identity(){return {*Name::parse("closure.value"),*OperationVersion::parse("1.0.0",5),{}};}
  static constexpr auto async_ownership=AsyncOwnership::Owning;
  static Result<void> validate(const native_test::closure::Value& v){
    if(v.value==991)throw std::bad_alloc{};
    if(v.value==992)throw std::runtime_error("outcome construction failure");
    return {};
  }
};
}

namespace native_test::closure {
using Table=executions::detail::ExecutionTable;
using Managed=executions::detail::ManagedExecution<int,int>;
struct Executor final : ExecutorPort {
  bool reject=false;
  Result<void> submit(std::unique_ptr<ReadyWork> work) override {
    if(reject)return make_unexpected(error(ContractsErrc::Rejected));
    work->execute();return {};
  }
};
struct Reader {};
using AsyncCall=AsyncReadCall<Input,Value,Reader>;
std::shared_ptr<AsyncCall> pending;
Result<void> async_handler(std::shared_ptr<AsyncCall> call){pending=std::move(call);return {};}
ExecutionRef identity(unsigned n) {
  ExecutionRef ref;for(unsigned i=0;i<4;++i)ref.execution_id.bytes[i]=static_cast<unsigned char>(n>>(8*i));return ref;
}
Managed::Record::Policy storage() {
  return {sizeof(int),sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return sizeof(int);},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
}
struct Harness {
  Env env;
  Result<invocation::NativeBound<int,int>> bound=env.bind();
  HostIncarnation host;
  std::shared_ptr<Table> table;
  std::shared_ptr<Executor> executor=std::make_shared<Executor>();
  std::shared_ptr<scheduler::Scheduler> schedule;
  std::shared_ptr<resources::ResourceManager> resources;
  explicit Harness(Table::Limits limits={}) {
    CHECK(bound);env.threads->role=ThreadRole::Worker;host.bytes[0]=111;
    auto t=Table::create(limits,host);CHECK(t);table=*t;
    auto s=scheduler::Scheduler::create(executor,{{policy_test::principal().principal_id}});CHECK(s);schedule=std::move(*s);
    auto r=resources::ResourceManager::create({{"closure.slot",1,false}});CHECK(r);resources=std::move(*r);
  }
  auto options() {auto result=env.options();result.deadline=env.policy.clock->now()+std::chrono::minutes(1);return result;}
  auto create(unsigned n,Managed::Record::Policy policy=storage(),int input=3) {
    return Managed::create(table,schedule,resources,identity(n),host,*bound,input,options(),policy,
        [](auto)->Result<std::vector<resources::Claim>>{return std::vector<resources::Claim>{};},[]{});
  }
  void finish(const std::shared_ptr<executions::detail::ManagedInvocation>& e) {
    e->drive();schedule->pump();e->drive();CHECK(e->finished());
  }
  ~Harness(){schedule->close();}
};
void check_failure(Harness& h,const std::shared_ptr<executions::detail::ManagedInvocation>& e,bool cancelled,bool entered) {
  auto ref=e->accepted().execution;
  auto summary=h.table->summary(e->entry());CHECK(summary&&(*summary)->value().execution==ref);
  CHECK((*summary)->value().phase==ExecutionPhase::Terminal&&(*summary)->value().version.value()>1);
  auto wait=h.table->wait_terminal(ref,std::chrono::steady_clock::now());CHECK(wait&&*wait==Table::WaitState::Terminal);
  auto value=h.table->result<int>(ref);CHECK(value&&std::holds_alternative<Completed<int>>(**value));
  const auto& outcome=std::get<Completed<int>>(**value).outcome;
  CHECK(outcome.conditions().before_apply&&outcome.conditions().before_apply->execution_accepted);
  CHECK(outcome.conditions().before_apply->business_entered==entered);
  CHECK(cancelled?std::holds_alternative<CancelledBeforeApply>(outcome.value()):std::holds_alternative<FailedBeforeApply>(outcome.value()));
  ListRequest request{policy_test::principal(),PhaseSet::Terminal,{200,2000},{}};
  auto page=h.table->access_scan(request);CHECK(page);
  CHECK(std::any_of(page->candidates.begin(),page->candidates.end(),[&](const auto& x){return x.second.summary->value().execution==ref;}));
}
}
namespace native_test {
void execution_admission_endurance() {
  using namespace closure;Harness h;entered=0;
  for(unsigned n=1;n<=12000;++n) {
    const auto scans=h.table->reclamation().admission_pressure_scans;
    auto e=h.create(n);CHECK(e);h.finish(*e);
    CHECK(h.table->usage().records<=Table::Limits{}.records&&h.table->usage().input_bytes==0);
    CHECK(h.table->reclamation().admission_pressure_scans-scans<=Table::Limits{}.scan_limit);
  }
  CHECK(entered==12000);
  CHECK(h.table->reclamation().admission_pressure_evictions>=12000-Table::Limits{}.records);
  CHECK(!h.table->find(identity(1))&&h.table->find(identity(12000)));
  CHECK(h.table->reclamation().admission_pressure_failures==0);
}
void execution_admission_pins() {
  using namespace closure;
  for(bool reply_pressure:{false,true}) {
    Table::Limits limits;limits.records=reply_pressure?10:2;limits.reply_bytes=2*sizeof(InvokeReply<int>);
    limits.scan_limit=4;Harness h(limits);
    std::vector<std::shared_ptr<const InvokeReply<int>>> pins;
    for(unsigned n=1;n<=2;++n) {auto e=h.create(n);CHECK(e);h.finish(*e);auto p=h.table->result<int>(identity(n));CHECK(p);pins.push_back(*p);}
    const auto before=h.table->reclamation();CHECK(!h.create(3));
    auto after=h.table->reclamation();CHECK(after.admission_pressure_scans-before.admission_pressure_scans<=limits.scan_limit);
    CHECK(h.table->find(identity(1))&&h.table->find(identity(2)));CHECK(result(*pins[0])==5);
    CHECK(after.admission_pressure_failures==1&&after.pinned_terminal_skips>=2);
    ListRequest request{policy_test::principal(),PhaseSet::Terminal,{1,4},{}};
    auto page=h.table->access_scan(request);CHECK(page&&page->next_scan);request.position=page->next_scan;
    pins[0].reset();auto accepted=h.create(3);CHECK(accepted);h.finish(*accepted);
    CHECK(!h.table->find(identity(1))&&h.table->find(identity(2)));
    auto next=h.table->access_scan(request);CHECK(next&&next->candidates.empty()&&!next->next_scan);
    CHECK(h.table->reclamation().admission_pressure_evictions==1);
  }
}
void execution_reclaim_protection() {
  using namespace closure;Table::Limits limits;limits.records=3;limits.scan_limit=2;
  Harness h(limits);
  auto prepare=[&](unsigned n,std::shared_ptr<void> payload) {
    return h.table->prepare({identity(n),key(),policy_test::principal(),{},ExecutionPhase::Queued,h.host,*ObservationVersion::create(1),{},{}},
        std::move(payload),CppTypeToken::of<int>(),4,16);
  };
  auto hidden=prepare(1,std::make_shared<int>(1));CHECK(hidden);
  auto active=prepare(2,std::make_shared<int>(2));CHECK(active);CHECK(h.table->publish(*active));
  auto finalizing=prepare(3,std::make_shared<int>(3));CHECK(finalizing);CHECK(h.table->publish(*finalizing));
  CHECK(h.table->transition(*finalizing,ExecutionPhase::Finalizing,{{RequiredRecordState::NotRequired,1,0},{},{},false,false}));
  hidden->reset();active->reset();finalizing->reset();
  for(unsigned n=4;n<8;++n)CHECK(!prepare(n,std::make_shared<int>(4)));
  CHECK(h.table->usage().records==3&&h.table->reclamation().terminal_evictions==0);
  CHECK(h.table->find(identity(2))&&h.table->find(identity(3))&&!h.table->find(identity(1)));
  // 新表的最后 payload 析构主动重入表：压力回收必须在表锁外销毁。
  Table::Limits one;one.records=1;Harness reentrant(one);bool destroyed=false;
  auto payload=std::shared_ptr<int>(new int(1),[&](int* p){delete p;CHECK(reentrant.table->usage().records==1);CHECK(!reentrant.table->find(identity(8)));destroyed=true;});
  auto e=reentrant.table->prepare({identity(8),key(),policy_test::principal(),{},ExecutionPhase::Queued,reentrant.host,*ObservationVersion::create(1),{},{}},payload,CppTypeToken::of<int>(),4,16);CHECK(e);
  CHECK(reentrant.table->publish(*e));PhaseConditions done{{RequiredRecordState::NotRequired,0,0},{},{},false,false};
  CHECK(reentrant.table->transition(*e,ExecutionPhase::Finalizing,done));CHECK(reentrant.table->transition(*e,ExecutionPhase::Terminal,done));
  payload.reset();e->reset();auto next=reentrant.create(9);CHECK(next&&destroyed);reentrant.finish(*next);
}
void managed_accepted_failures() {
  using namespace closure;
  {Harness h;CHECK(!h.create(1,storage(),-1));CHECK(!h.table->find(identity(1)));}
  {Harness h;h.executor->reject=true;auto e=h.create(1);CHECK(e);h.finish(*e);check_failure(h,*e,false,false);}
  {Harness h;auto e=h.create(1);CHECK(e);CHECK((*e)->cancel());check_failure(h,*e,true,false);}
  {Harness h;auto options=h.options();
    auto e=Managed::create(h.table,h.schedule,h.resources,identity(1),h.host,*h.bound,3,options,storage(),
        [](auto)->Result<std::vector<resources::Claim>>{return std::vector<resources::Claim>{};},[]{});CHECK(e);
    h.schedule->pump(options.deadline);CHECK((*e)->finished());check_failure(h,*e,true,false);}
  {Harness h;
    auto e=Managed::create(h.table,h.schedule,h.resources,identity(1),h.host,*h.bound,3,h.options(),storage(),
        [](auto)->Result<std::vector<resources::Claim>>{return std::vector<resources::Claim>{{"absent",resources::Mode::Exclusive,1}};},[]{});
    CHECK(e);h.finish(*e);check_failure(h,*e,false,false);}
  {Harness h;auto e=h.create(1);CHECK(e);auto denied=policy_test::configuration().principals[0];denied.rules.clear();
    CHECK(h.env.policy.assembly.administration->replace_principal_policy(denied));h.finish(*e);check_failure(h,*e,false,false);}
  for(unsigned mode=0;mode<3;++mode) {
    Harness h;auto policy=storage();
    if(mode==0)policy.reply_limit=1;
    if(mode==1)policy.reply_bytes=[](const InvokeReply<int>&)->Result<std::size_t>{return make_unexpected(error(ContractsErrc::BudgetExceeded));};
    if(mode==2)policy.reply_bytes=[](const InvokeReply<int>&)->Result<std::size_t>{throw std::bad_alloc{};};
    auto e=h.create(1,policy);CHECK(e);h.finish(*e);check_failure(h,*e,false,true);
    h.env.threads->role=ThreadRole::Application;
    CHECK(std::holds_alternative<Rejected>(h.bound->invoke(-1,h.options())));
  }
}
void managed_async_input_accounting() {
  using namespace closure;
  for(int result_value:{7,991,992}) {
    auto reader=std::make_shared<closure::Reader>();
    Env env(true,compute,{},[&](registry::ModuleInput& module){
      module.services.clear();module.services.push_back(*registry::ServiceBinding::make(name("reader"),reader));
      module.manifest.required_services[0].type=CppTypeToken::of<closure::Reader>();
      module.executors[0].async_dispatch=true;module.executors[0].external_wait=true;
      module.register_operations=[](registry::Registrar& registrar){
        DefinitionInput definition{key(),{},{false,true,true,name("test"),name("app")},AtomicMode::Incompatible,{name("allow")},"closure.async"};
        registry::OperationOptions options{{{name("native"),name("reader")}},{},{name("native"),name("test")},{},false};
        registry::SubmissionStorage<Input,Value> storage{65536,sizeof(InvokeReply<Value>),
            [](const Input& input)->Result<std::size_t>{return sizeof(Input)+input.values->capacity()*sizeof(int);},
            [](const InvokeReply<Value>&)->Result<std::size_t>{return sizeof(InvokeReply<Value>);}};
        CHECK(registrar.read_async(closure::async_handler,definition,options,storage));
      };
    });
    env.threads->role=ThreadRole::Worker;
    auto projection=+[](const Input&,std::span<foundation::ObjectId> out) noexcept ->Result<std::size_t>{return targets(0,out);};
    auto bound=env.engine->bind<Input,Value>(key(),{},Shape::Read,env.policy.caller,std::array{policy_test::target()},projection,name("closure.async"));CHECK(bound);
    HostIncarnation host;host.bytes[0]=113;Table::Limits limits;limits.input_bytes=sizeof(Input)+8192*sizeof(int);
    auto table=Table::create(limits,host);CHECK(table);
    auto executor=std::make_shared<closure::Executor>();
    auto schedule=scheduler::Scheduler::create(executor,{{policy_test::principal().principal_id}});CHECK(schedule);
    std::shared_ptr<scheduler::Scheduler> scheduler=std::move(*schedule);
    const auto resolve=[](auto)->Result<std::vector<resources::Claim>>{return std::vector<resources::Claim>{};};
    auto input=std::make_shared<const std::vector<int>>(8192,1);std::weak_ptr<const std::vector<int>> weak=input;
    auto record=executions::detail::InvocationAccess::registered_record(*bound,Input{input},env.options());CHECK(record);
    auto execution=executions::detail::ManagedInvocation::create(*table,scheduler,{},identity(1),host,*record,resolve,[]{});CHECK(execution);
    input.reset();CHECK(!weak.expired());(*execution)->drive();scheduler->pump();(*execution)->drive();CHECK(pending);
    CHECK(pending->complete(Value{result_value}));(*execution)->drive();
    CHECK(!(*record)->settled()&&!weak.expired()&&(*table)->usage().input_bytes==limits.input_bytes);
    auto summary=(*table)->summary((*execution)->entry());CHECK(summary&&(*summary)->value().phase==ExecutionPhase::Finalizing);
    pending.reset();CHECK(weak.expired()&&(*record)->settled());(*execution)->drive();CHECK((*execution)->finished());
    CHECK((*table)->usage().input_bytes==0&&(*table)->usage().records==1);
    (*table)->release_input_charge((*execution)->entry());CHECK((*table)->usage().input_bytes==0); // 幂等。
    auto pin=(*table)->result<Value>(identity(1));CHECK(pin&&std::holds_alternative<Completed<Value>>(**pin));
    const auto& outcome=std::get<Completed<Value>>(**pin).outcome;
    if(result_value==7)CHECK(std::holds_alternative<ReadCompleted<Value>>(outcome.value()));
    else {
      CHECK(std::holds_alternative<FailedBeforeApply>(outcome.value()));
      CHECK(outcome.conditions().before_apply->business_entered&&outcome.conditions().before_apply->execution_accepted);
    }
    // 原结果和执行 owner 仍被 pin，相同大输入已能再次接收。
    auto again=executions::detail::InvocationAccess::registered_record(*bound,Input{std::make_shared<const std::vector<int>>(8192,2)},env.options());CHECK(again);
    auto next=executions::detail::ManagedInvocation::create(*table,scheduler,{},identity(2),host,*again,resolve,[]{});CHECK(next);
    (*next)->drive();scheduler->pump();CHECK(pending);CHECK(pending->complete(Value{7}));pending.reset();(*next)->drive();
    CHECK((*next)->finished()&&(*table)->usage().input_bytes==0&&(*table)->usage().records==2);
    scheduler->close();
  }
}
}
