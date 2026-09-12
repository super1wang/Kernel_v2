#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/domain.hpp>
#include <functional>

struct Config {std::vector<int> values;};
template<> struct ock::state::RootContract<Config> {
  static Result<Config> freeze(const Config& v) {return Config{v.values};}
  static Result<std::size_t> bytes(const Config& v) {return sizeof(v)+v.values.capacity()*sizeof(int);}
};
struct Issuer final:CallerAuthorityPort {
  struct Grant final:CallerGrant {CallerDescription d;explicit Grant(CallerDescription v):d(std::move(v)){}
    const CallerDescription& description() const noexcept override{return d;}};
  std::shared_ptr<Grant> grant;
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription& d) override {
    grant=std::make_shared<Grant>(d);return std::shared_ptr<const CallerGrant>(grant);}
  Result<void> validate(const CallerGrant& value) const override {return grant.get()==&value?Result<void>{}:reject(ContractsErrc::InvalidGrant);}
};
struct Reads final:ock::state::SnapshotAuthority {
  std::shared_ptr<Issuer> issuer;explicit Reads(std::shared_ptr<Issuer> v):issuer(std::move(v)){}
  Result<void> authorize(const CallerView& c,const AtomicDomainRef& d) const override {
    if(d!=domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*issuer,c);}
};
struct Permit final:ActionPermit {PermitBinding value;explicit Permit(PermitBinding v):value(std::move(v)){}
  const PermitBinding& binding() const noexcept override{return value;}};
struct Permits final:PermitAuthorityPort {
  int consumes=0;bool reject_consume=false;std::function<void()> callback;
  Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding& b) override {
    return std::shared_ptr<const ActionPermit>(std::make_shared<Permit>(b));}
  Result<void> consume(const ActionPermit& p,const PermitBinding& expected) override {
    ++consumes;if(callback)callback();
    if(reject_consume||p.binding()!=expected)return reject(ContractsErrc::InvalidGrant);return {};}
};
static PreparedIdentity attempt(unsigned n,std::uint64_t revision) {
  return {id<CommitId>(n),id<ReservationId>(n),domain(),revision,1};
}
int main() try {
  auto issuer=std::make_shared<Issuer>();PrincipalRef principal{id<PrincipalId>()};
  auto grant=issuer->authenticate({principal,{},{}});CHECK(grant);
  auto caller=CallerView::check(issuer,*grant);CHECK(caller);
  auto reads=std::make_shared<Reads>(issuer);
  ock::state::DomainOptions options{.root_bytes=4096,.candidate_bytes=4096,.result_bytes=256,
    .history_entries=4,.history_bytes=4096,.snapshot_pins=8,.history_pins=2,.inflight_commits=1,.reclaim_batch=1};
  using Domain=ock::state::StateDomain<Config>;
  auto made=Domain::create(domain(),Config{{1}},options,reads);CHECK(made);auto state=*made;
  auto base=state->snapshot(*caller);CHECK(base);
  auto first=state->prepare(*base,Config{{2}},attempt(1,0),64);CHECK(first);
  CHECK(!state->prepare(*base,Config{{3}},attempt(2,0),64)); // 同域 reservation 唯一。
  OperationKey op{name("state.replace"),ver()};ContractDigest group{};
  PermitBinding binding{principal,op,group,domain().domain_id,1,1,std::chrono::steady_clock::now()+std::chrono::minutes(1)};
  auto permit=std::make_shared<Permit>(binding);Permits permits;
  auto committed=state->commit(*first,permit,permits,binding);CHECK(committed&&permits.consumes==1);
  CHECK(committed->publication.revision==1&&committed->commit.revision==1&&committed->published.published_version==1);
  CHECK(state->validate(*committed->proof,committed->publication));
  auto now=state->snapshot(*caller);CHECK(now&&now->revision()==1&&now->history_cursor()==1&&now->value().values[0]==2);
  CHECK(!state->prepare(*base,Config{{9}},attempt(3,0),64)); // 旧 revision 不重算。
  auto empty=state->prepare(*now,now->value(),attempt(4,1),0);CHECK(empty);
  auto empty_commit=state->commit(*empty,std::make_shared<Permit>(binding),permits,binding);CHECK(empty_commit);
  auto revision2=state->snapshot(*caller);CHECK(revision2&&revision2->revision()==2&&revision2->value().values[0]==2);
  {
    auto h1=state->history(*caller,1),h2=state->history(*caller,2);CHECK(h1&&h2);
    CHECK(h1->record().before().values[0]==1&&h1->record().after().values[0]==2);
    CHECK(h2->record().delta_bytes()==0&&!state->history(*caller,1));
  }
  CHECK(state->history(*caller,1)); // pin 释放后恢复。

  auto failed=state->prepare(*revision2,Config{{7}},attempt(5,2),64);CHECK(failed);
  permits.reject_consume=true;CHECK(!state->commit(*failed,std::make_shared<Permit>(binding),permits,binding));permits.reject_consume=false;
  auto unchanged=state->snapshot(*caller);CHECK(unchanged&&unchanged->revision()==2&&unchanged->value().values[0]==2);

  auto normal=state->prepare(*unchanged,Config{{7}},attempt(6,2),64);CHECK(normal);
  CHECK(state->commit(*normal,std::make_shared<Permit>(binding),permits,binding));
  auto revision3=state->snapshot(*caller);CHECK(revision3&&revision3->revision()==3&&revision3->value().values[0]==7);
  auto undo=state->prepare_undo(*revision3,attempt(7,3));CHECK(undo);
  CHECK(state->commit(*undo,std::make_shared<Permit>(binding),permits,binding));
  auto revision4=state->snapshot(*caller);CHECK(revision4&&revision4->revision()==4&&revision4->value().values[0]==2);
  CHECK(!state->prepare_undo(*revision4,attempt(8,4)));
  auto redo=state->prepare_redo(*revision4,attempt(9,4));CHECK(redo);
  CHECK(state->commit(*redo,std::make_shared<Permit>(binding),permits,binding));
  auto revision5=state->snapshot(*caller);CHECK(revision5&&revision5->revision()==5&&revision5->value().values[0]==7);

  auto closing=state->prepare(*revision5,Config{{8}},attempt(10,5),64);CHECK(closing);
  permits.callback=[&]{state->close();};
  auto claimed=state->commit(*closing,std::make_shared<Permit>(binding),permits,binding);CHECK(claimed);permits.callback={};
  CHECK(state->validate(*claimed->proof,claimed->publication));CHECK(!state->snapshot(*caller));

  auto closed_first=Domain::create(domain(),Config{{1}},options,reads);CHECK(closed_first);
  auto cb=(*closed_first)->snapshot(*caller);CHECK(cb);
  auto cp=(*closed_first)->prepare(*cb,Config{{2}},attempt(11,0),64);CHECK(cp);
  (*closed_first)->close();CHECK(!(*closed_first)->commit(*cp,permit,permits,binding));
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
