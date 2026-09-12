#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/atomic.hpp>
#include <ock/state/provider.hpp>

using ock::state::AtomicDescription;using ock::state::AtomicForm;
struct Item {int number;};
struct Command {foundation::ObjectId id;int number;bool fail=false;};
struct Metric {int number;};
template<> struct ock::state::RootContract<Item>{static Result<Item> freeze(const Item& v){return v;}static Result<std::size_t> bytes(const Item&){return sizeof(Item);}};
template<> struct ock::state::RootContract<Command>{static Result<Command> freeze(const Command& v){return v;}static Result<std::size_t> bytes(const Command&){return sizeof(Command);}};
template<> struct ock::state::RootContract<Metric>{static Result<Metric> freeze(const Metric& v){return v;}static Result<std::size_t> bytes(const Metric&){return sizeof(Metric);}};
template<> struct ock::contracts::TypeContract<Item>{static TypeIdentity identity(){return{name("atomic.item"),ver(),{}};}static Result<void> validate(const Item&){return{};}};
template<> struct ock::contracts::TypeContract<Command>{static TypeIdentity identity(){return{name("atomic.command"),ver(),{}};}static Result<void> validate(const Command& v){return v.id.empty()?reject(ContractsErrc::InvalidContract):Result<void>{};}};
template<> struct ock::contracts::TypeContract<Metric>{static TypeIdentity identity(){return{name("atomic.metric"),ver(),{}};}static Result<void> validate(const Metric&){return{};}};
static int edit_entries=0;
static Result<Metric> put(const Command& c,EditView<ock::state::ObjectStateProvider>& view,WorkContext&) {
  ++edit_entries;if(c.fail)return make_unexpected(error(ContractsErrc::Rejected));
  auto value=ock::state::ObjectValue::freeze(Item{c.number},1024);if(!value)return make_unexpected(value.error());
  auto record=ock::state::ObjectRecord::create(c.id,*value,{},4);if(!record)return make_unexpected(record.error());
  auto prior=view.edit().find(c.id);auto changed=prior?view.edit().replace(*record):view.edit().create(*record);
  if(!changed)return make_unexpected(changed.error());return Metric{c.number};
}
static Result<Metric> read_candidate(const Command& c,const ock::state::ObjectStateProvider::CandidateReadPort& read,WorkContext&) {
  auto record=read.find(c.id);if(!record)return make_unexpected(error(ContractsErrc::Rejected));
  auto item=record->value().get<Item>();if(!item)return make_unexpected(error(ContractsErrc::TypeMismatch));return Metric{item->number};
}
static Result<Metric> compute(const Command& c,WorkContext&){return Metric{c.number*2};}
struct Issuer final:CallerAuthorityPort {
  struct Grant final:CallerGrant{CallerDescription d;explicit Grant(CallerDescription v):d(std::move(v)){}const CallerDescription& description()const noexcept override{return d;}};
  std::shared_ptr<Grant> grant;Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription& d)override{grant=std::make_shared<Grant>(d);return std::shared_ptr<const CallerGrant>(grant);}
  Result<void> validate(const CallerGrant& g)const override{return grant.get()==&g?Result<void>{}:reject(ContractsErrc::InvalidGrant);}
};
static AtomicDomainRef object_domain(){auto d=domain();d.provider=ProviderContract<ock::state::ObjectStateProvider>::key();return d;}
struct Reads final:ock::state::SnapshotAuthority{std::shared_ptr<Issuer> issuer;explicit Reads(std::shared_ptr<Issuer> v):issuer(std::move(v)){}
  Result<void> authorize(const CallerView& c,const AtomicDomainRef& d)const override{return d==object_domain()?validate_caller(*issuer,c):reject(ContractsErrc::InvalidGrant);}};
struct Permit final:ActionPermit{PermitBinding b;explicit Permit(PermitBinding v):b(std::move(v)){}const PermitBinding& binding()const noexcept override{return b;}};
struct Permits final:PermitAuthorityPort{int count=0;Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding& b)override{return std::shared_ptr<const ActionPermit>(std::make_shared<Permit>(b));}
  Result<void> consume(const ActionPermit& p,const PermitBinding& b)override{++count;return p.binding()==b?Result<void>{}:reject(ContractsErrc::InvalidGrant);}};
struct Authority final:ock::state::AtomicAuthorityPort {
  mutable int calls=0;int fail_at=0;
  Result<void> preflight(const AtomicDomainRef& d,std::uint64_t,std::span<const AtomicDescription> steps,std::span<const foundation::ObjectId> resources)const override {
    if(d!=object_domain()||steps.empty()||!resources.empty())return reject(ContractsErrc::InvalidGrant);
    for(const auto& step:steps)if(step.domain!=d||step.target!=d.domain_id)return reject(ContractsErrc::InvalidGrant);return{};}
  Result<void> authorize(const AtomicDescription&,WorkContext&)const override{++calls;return fail_at==calls?reject(ContractsErrc::InvalidGrant):Result<void>{};}
};
struct Receiver final:CommitReceiver {int calls=0;std::optional<CommitReport> report;
  void completed(CommitReport value) noexcept override{++calls;report=std::move(value);}};
static AtomicDescription description(AtomicMode mode,Shape shape){return {{name(mode==AtomicMode::StateEdit?"state.put":mode==AtomicMode::CandidateRead?"state.read":"state.compute"),ver()},
  {},shape,mode,mode==AtomicMode::PureCompute?std::optional<AtomicProviderKey>{}:object_domain().provider,object_domain(),object_domain().domain_id};}
static PreparedIdentity attempt(unsigned n,std::uint64_t revision){return{id<CommitId>(n),id<ReservationId>(n),object_domain(),revision,1};}
int main() try {
  auto issuer=std::make_shared<Issuer>();PrincipalRef principal{id<PrincipalId>()};auto grant=issuer->authenticate({principal,{},{}});CHECK(grant);auto caller=CallerView::check(issuer,*grant);CHECK(caller);
  ock::state::DomainOptions domain_options{.root_bytes=64*1024*1024,.candidate_bytes=64*1024*1024,.result_bytes=4096,.history_entries=8,.history_bytes=64*1024,.snapshot_pins=16,.history_pins=4,.inflight_commits=1,.reclaim_batch=2};
  auto made=ock::state::StateDomain<ock::state::ObjectRoot>::create(object_domain(),{},domain_options,std::make_shared<Reads>(issuer));CHECK(made);
  auto authority=std::make_shared<Authority>();ock::state::AtomicOptions atomic_options{128,16384,128,{64*1024*1024,64*1024*1024,128}};
  ock::state::ObjectAtomic atomic(*made,authority,atomic_options);auto object=id<foundation::ObjectId>(9);
  auto edit=ock::state::AtomicStep::state_edit(description(AtomicMode::StateEdit,Shape::StateEdit),Command{object,4},put,1024,1024);CHECK(edit);
  auto read=ock::state::AtomicStep::candidate_read(description(AtomicMode::CandidateRead,Shape::Read),Command{object,0},read_candidate,1024,1024);CHECK(read);
  auto pure=ock::state::AtomicStep::pure_compute(description(AtomicMode::PureCompute,Shape::Read),Command{object,3},compute,1024,1024);CHECK(pure);
  std::array steps{*edit,*read,*pure};OperationKey op{name("atomic.group"),ver()};ContractDigest group{};
  PermitBinding binding{principal,op,group,object_domain().domain_id,1,1,std::chrono::steady_clock::now()+std::chrono::minutes(1)};
  Permits permits;auto budget=*foundation::CheckedCount<std::uint64_t>::create(0,100);WorkContext work({},binding.deadline,budget,name("atomic.test"),{});
  auto done=atomic.execute(*caller,steps,{},attempt(1,0),std::make_shared<Permit>(binding),permits,binding,work);CHECK(done);
  CHECK(done->values.size()==3&&done->values[1].get<Metric>()->number==4&&done->values[2].get<Metric>()->number==6);
  CHECK(done->commit.publication.revision==1&&permits.count==1&&edit_entries==1);
  auto after=(*made)->snapshot(*caller);CHECK(after&&after->revision()==1&&after->history_cursor()==1);

  authority->fail_at=authority->calls+2;
  auto failed=atomic.execute(*caller,steps,{},attempt(2,1),std::make_shared<Permit>(binding),permits,binding,work);CHECK(!failed&&permits.count==1);
  auto unchanged=(*made)->snapshot(*caller);CHECK(unchanged&&unchanged->revision()==1&&unchanged->value().find(object)->value().get<Item>()->number==4);
  authority->fail_at=0;
  auto bad_edit=ock::state::AtomicStep::state_edit(description(AtomicMode::StateEdit,Shape::StateEdit),Command{object,8,true},put,1024,1024);CHECK(bad_edit);
  CHECK(!atomic.execute(*caller,std::span<const ock::state::AtomicStep>(&*bad_edit,1),{},attempt(3,1),std::make_shared<Permit>(binding),permits,binding,work));
  CHECK((*made)->snapshot(*caller)->revision()==1&&permits.count==1);
  CHECK(!atomic.execute(*caller,std::span<const ock::state::AtomicStep>(&*pure,1),{},attempt(4,1),std::make_shared<Permit>(binding),permits,binding,work));

  auto before_entries=edit_entries;
  for(auto form:{AtomicForm::Await,AtomicForm::Ticket,AtomicForm::Nested,AtomicForm::Conditional,AtomicForm::Loop,AtomicForm::Parallel}) {
    auto forbidden=description(AtomicMode::StateEdit,Shape::StateEdit);forbidden.form=form;
    CHECK(!ock::state::AtomicStep::state_edit(forbidden,Command{object,5},put,1024,1024));
  }
  for(auto malformed:{0,1,2}) {
    auto forbidden=description(AtomicMode::StateEdit,Shape::StateEdit);
    if(malformed==0)forbidden.complete=false;
    if(malformed==1)forbidden.async_dispatch=true;
    if(malformed==2)forbidden.external_wait=true;
    CHECK(!ock::state::AtomicStep::state_edit(forbidden,Command{object,5},put,1024,1024));
  }
  auto wrong_shape=description(AtomicMode::StateEdit,Shape::Read);
  CHECK(!ock::state::AtomicStep::state_edit(wrong_shape,Command{object,5},put,1024,1024));
  CHECK(edit_entries==before_entries);
  std::vector<ock::state::AtomicStep> too_many(129,*edit);
  CHECK(!atomic.execute(*caller,too_many,{},attempt(5,1),std::make_shared<Permit>(binding),permits,binding,work));

  auto single=atomic.execute(*caller,std::span<const ock::state::AtomicStep>(&*edit,1),{},attempt(6,1),std::make_shared<Permit>(binding),permits,binding,work);CHECK(single);
  CHECK((*made)->snapshot(*caller)->revision()==2&&permits.count==2&&edit_entries==before_entries+1);

  auto provider_domain=ock::state::StateDomain<ock::state::ObjectRoot>::create(object_domain(),{},domain_options,std::make_shared<Reads>(issuer));CHECK(provider_domain);
  auto provider_permits=std::make_shared<Permits>();
  auto provider=ock::state::ObjectMemoryProvider::create(*provider_domain,atomic_options.edit);CHECK(provider);
  auto frame=(*provider)->begin(object_domain(),*caller);CHECK(frame);
  EditView<ock::state::ObjectStateProvider> provider_view((*frame)->edit,object_domain());
  CHECK(put(Command{object,11},provider_view,work));CHECK((*frame)->candidate().find(object));
  auto public_prepared=(*provider)->prepare(**frame,attempt(20,0));CHECK(public_prepared);
  auto receiver=std::make_shared<Receiver>();
  auto forged=PreparedCommit::create(attempt(20,0),{},64);CHECK(forged);
  CHECK(!(*provider)->commit(*forged,std::make_shared<Permit>(binding),provider_permits,binding,receiver)&&receiver->calls==0);
  CHECK((*provider)->commit(*public_prepared,std::make_shared<Permit>(binding),provider_permits,binding,receiver));
  CHECK(receiver->calls==1&&receiver->report->disposition==CommitDisposition::Published);
  CHECK((*provider)->published(attempt(20,0))&&(*provider_domain)->snapshot(*caller)->value().find(object));
  CHECK(!(*provider)->commit(*public_prepared,std::make_shared<Permit>(binding),provider_permits,binding,receiver)&&receiver->calls==1);
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
