#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/domain.hpp>
#include <atomic>
#include <functional>
#include <thread>
#include <cstdlib>
static std::atomic<bool> fail_next_allocation=false;
void* operator new(std::size_t size){if(fail_next_allocation.exchange(false))throw std::bad_alloc{};if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc{};}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}

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
  std::shared_ptr<Issuer> issuer;bool throw_auth=false;explicit Reads(std::shared_ptr<Issuer> v):issuer(std::move(v)){}
  Result<void> authorize(const CallerView& c,const AtomicDomainRef& d) const override {
    if(throw_auth)throw std::runtime_error("snapshot authority");
    if(d!=domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*issuer,c);}
};
struct Permit final:ActionPermit {PermitBinding value;explicit Permit(PermitBinding v):value(std::move(v)){}
  const PermitBinding& binding() const noexcept override{return value;}};
struct Permits final:PermitAuthorityPort {
  Result<void> consume_claimed(const ActionPermit& p,const PermitBinding& b,CommitClaim& claim) override {
    auto checked=consume(p,b);if(!checked)return checked;
    if(!claim.try_claim())return reject(ContractsErrc::Rejected);
    if(after_claim)after_claim();return {};
  }
  std::function<void()> after_claim;
  int consumes=0;bool reject_consume=false,throw_consume=false;std::function<void()> callback;
  Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding& b) override {
    return std::shared_ptr<const ActionPermit>(std::make_shared<Permit>(b));}
  Result<void> consume(const ActionPermit& p,const PermitBinding& expected) override {
    ++consumes;if(callback)callback();
    if(throw_consume)throw std::runtime_error("permit callback");
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
    .history_entries=4,.history_bytes=4096,.snapshot_pins=16,.history_pins=2,.inflight_commits=1,.reclaim_batch=1};
  using Domain=ock::state::StateDomain<Config>;
  auto unsupported=options;unsupported.inflight_commits=2;
  CHECK(!Domain::create(domain(),Config{{1}},unsupported,reads)); // 首版每域只允许一个 reservation。
  auto made=Domain::create(domain(),Config{{1}},options,reads);CHECK(made);auto state=*made;
  reads->throw_auth=true;CHECK(!state->snapshot(*caller));reads->throw_auth=false;
  auto base=state->snapshot(*caller);CHECK(base);
  auto first=state->prepare(*base,Config{{2}},attempt(1,0),64);CHECK(first);
  CHECK(!state->prepare(*base,Config{{3}},attempt(2,0),64)); // 同域 reservation 唯一。
  OperationKey op{name("state.replace"),ver()};ContractDigest group{};
  PermitBinding binding{principal,op,group,domain().domain_id,1,1,std::chrono::steady_clock::now()+std::chrono::minutes(1)};
  auto permit=std::make_shared<Permit>(binding);Permits permits;
  auto committed=state->commit(*first,permit,permits,binding);CHECK(committed&&permits.consumes==1);
  CHECK(committed->publication.revision==1&&committed->commit.revision==1&&committed->published.published_version==1);
  CHECK(state->validate(*committed->proof,committed->publication));
  auto forged_publication=committed->publication;forged_publication.commit=id<CommitId>(200);
  CHECK(!state->attest(forged_publication));
  auto now=state->snapshot(*caller);CHECK(now&&now->revision()==1&&now->history_cursor()==1&&now->value().values[0]==2);
  CHECK(!state->prepare(*base,Config{{9}},attempt(3,0),64)); // 旧 revision 不重算。
  auto empty=state->prepare(*now,now->value(),attempt(4,1),0);CHECK(empty);
  auto empty_commit=state->commit(*empty,std::make_shared<Permit>(binding),permits,binding);CHECK(empty_commit);
  auto revision2=state->snapshot(*caller);CHECK(revision2&&revision2->revision()==2&&revision2->value().values[0]==2);
  {
    auto h1=state->history(*caller,1),h2=state->history(*caller,2);CHECK(h1&&h2);
    CHECK(h1->record().before().values[0]==1&&h1->record().after().values[0]==2);
    CHECK(h2->record().delta_bytes()==0&&!state->history(*caller,1)); // 两个 history pin 已占满。
  }
  CHECK(state->history(*caller,1)); // pin 释放后恢复。

  auto failed=state->prepare(*revision2,Config{{7}},attempt(5,2),64);CHECK(failed);
  permits.reject_consume=true;CHECK(!state->commit(*failed,std::make_shared<Permit>(binding),permits,binding));permits.reject_consume=false;
  auto unchanged=state->snapshot(*caller);CHECK(unchanged&&unchanged->revision()==2&&unchanged->value().values[0]==2);

  auto throwing=state->prepare(*unchanged,Config{{6}},attempt(16,2),64);CHECK(throwing);
  permits.throw_consume=true;CHECK(!state->commit(*throwing,std::make_shared<Permit>(binding),permits,binding));permits.throw_consume=false;
  auto still_unchanged=state->snapshot(*caller);CHECK(still_unchanged&&still_unchanged->revision()==2&&still_unchanged->value().values[0]==2);

  auto proof_oom=state->prepare(*still_unchanged,Config{{7}},attempt(17,2),64);CHECK(proof_oom);
  auto oom_permit=std::make_shared<Permit>(binding);fail_next_allocation=true;
  auto oom_result=state->commit(*proof_oom,oom_permit,permits,binding);CHECK(!oom_result&&!fail_next_allocation.load());
  CHECK(oom_result.error().code()==ock::state::error(ock::state::StateErrc::BudgetExceeded).code());
  auto normal=state->prepare(*still_unchanged,Config{{7}},attempt(6,2),64);CHECK(normal);
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
  permits.after_claim=[&]{state->close();};
  auto claimed=state->commit(*closing,std::make_shared<Permit>(binding),permits,binding);CHECK(claimed);permits.after_claim={};
  CHECK(state->validate(*claimed->proof,claimed->publication));CHECK(!state->snapshot(*caller));
  CHECK(!state->history(*caller,claimed->publication.revision));
  CHECK(state->reopen(Config{{1}},2));CHECK(state->validate(*claimed->proof,claimed->publication));
  auto reopened=state->snapshot(*caller);CHECK(reopened&&reopened->lifecycle_generation()==2&&reopened->revision()==0);

  auto closed_first=Domain::create(domain(),Config{{1}},options,reads);CHECK(closed_first);
  auto cb=(*closed_first)->snapshot(*caller);CHECK(cb);
  auto cp=(*closed_first)->prepare(*cb,Config{{2}},attempt(11,0),64);CHECK(cp);
  (*closed_first)->close();CHECK(!(*closed_first)->commit(*cp,permit,permits,binding));

  // 进入 consume 回调尚未 claim；close 先赢，不能发布。
  auto preclose=Domain::create(domain(),Config{{1}},options,reads);CHECK(preclose);
  auto pcb=(*preclose)->snapshot(*caller);CHECK(pcb);
  auto pcp=(*preclose)->prepare(*pcb,Config{{2}},attempt(12,0),64);CHECK(pcp);
  permits.callback=[&]{(*preclose)->close();};
  CHECK(!(*preclose)->commit(*pcp,permit,permits,binding));permits.callback={};
  CHECK(!(*preclose)->attest({attempt(12,0).commit,domain(),1,1}));

  auto abandoned=Domain::create(domain(),Config{{1}},options,reads);CHECK(abandoned);
  auto ab=(*abandoned)->snapshot(*caller);CHECK(ab);
  {auto lease=(*abandoned)->prepare(*ab,Config{{2}},attempt(13,0),64);CHECK(lease);}
  auto retry=(*abandoned)->prepare(*ab,Config{{3}},attempt(14,0),64);CHECK(retry);
  CHECK(!(*abandoned)->abandon(*pcp)); // 异主句柄不能取消真实候选。
  auto wrong_binding=binding;wrong_binding.target={};
  CHECK(!(*abandoned)->commit(*retry,permit,permits,wrong_binding));
  auto recovered=(*abandoned)->prepare(*ab,Config{{4}},attempt(15,0),64);CHECK(recovered);
  CHECK((*abandoned)->commit(*recovered,permit,permits,binding));

  // Byte pressure must reclaim before the ring wraps, but cannot oversell pinned owners.
  auto pressure_options=options;
  const auto row_bytes=64+sizeof(ock::state::HistoryRecord<Config>);
  pressure_options.history_entries=8;pressure_options.history_bytes=2*row_bytes;
  auto pressure=Domain::create(domain(),Config{{0}},pressure_options,reads);CHECK(pressure);
  auto publish_pressure=[&](unsigned n) {
    auto base=(*pressure)->snapshot(*caller);CHECK(base);
    auto prepared=(*pressure)->prepare(*base,Config{{static_cast<int>(n)}},attempt(150+n,n-1),64);CHECK(prepared);
    CHECK((*pressure)->commit(*prepared,permit,permits,binding));
  };
  publish_pressure(1);publish_pressure(2);publish_pressure(3);
  CHECK(!(*pressure)->history(*caller,1));
  CHECK((*pressure)->history_retained_bytes()==2*row_bytes);
  std::optional<ock::state::HistoryView<Config>> pin2;
  std::optional<ock::state::HistoryView<Config>> pin3;
  {auto h=(*pressure)->history(*caller,2);CHECK(h);pin2=*h;}
  {auto h=(*pressure)->history(*caller,3);CHECK(h);pin3=*h;}
  auto pressure_base=(*pressure)->snapshot(*caller);CHECK(pressure_base);
  CHECK(!(*pressure)->prepare(*pressure_base,Config{{4}},attempt(154,3),64));
  CHECK((*pressure)->history_retained_bytes()==2*row_bytes);
  pin2.reset();publish_pressure(4);
  CHECK((*pressure)->history_retained_bytes()==2*row_bytes);
  (*pressure)->close();CHECK((*pressure)->reopen(Config{{0}},2));
  CHECK((*pressure)->history_ring_bytes()==0&&(*pressure)->history_retained_bytes()==row_bytes);
  pin3.reset();CHECK((*pressure)->history_retained_bytes()==0);

  // 读线程只能看到完整发布的 immutable root，不得观察到半次提交。
  auto concurrent=Domain::create(domain(),Config{{0,0}},options,reads);CHECK(concurrent);
  std::atomic<bool> writer_done=false;std::atomic<bool> torn=false;
  std::jthread reader([&] {
    std::uint64_t observed=0;
    while(!writer_done.load(std::memory_order_acquire)) {
      auto sample=(*concurrent)->snapshot(*caller);
      if(!sample||sample->revision()<observed||sample->value().values.size()!=2||
          sample->value().values[0]!=sample->value().values[1]) {torn.store(true,std::memory_order_release);break;}
      observed=sample->revision();
    }
  });
  for(unsigned n=1;n<=100;++n) {
    auto prior=(*concurrent)->snapshot(*caller);CHECK(prior);
    auto prepared=(*concurrent)->prepare(*prior,Config{{static_cast<int>(n),static_cast<int>(n)}},attempt(20+n,n-1),64);CHECK(prepared);
    CHECK((*concurrent)->commit(*prepared,std::make_shared<Permit>(binding),permits,binding));
  }
  writer_done.store(true,std::memory_order_release);reader.join();CHECK(!torn.load(std::memory_order_acquire));
  auto final=(*concurrent)->snapshot(*caller);CHECK(final&&final->revision()==100&&final->value().values==std::vector<int>({100,100}));
  auto reopening=Domain::create(domain(),Config{{1}},options,reads);CHECK(reopening);
  std::atomic<bool> reopen_done=false,reopen_torn=false;
  std::jthread observing([&]{
    while(!reopen_done.load()) {
      auto view=(*reopening)->snapshot(*caller);
      if(view&&view->value().values[0]!=static_cast<int>(view->lifecycle_generation()))reopen_torn=true;
    }
  });
  for(unsigned generation=2;generation<=100;++generation){(*reopening)->close();CHECK((*reopening)->reopen(Config{{static_cast<int>(generation)}},generation));}
  reopen_done=true;observing.join();CHECK(!reopen_torn);
  auto current_generation=(*reopening)->snapshot(*caller);CHECK(current_generation&&(*reopening)->validate_base(*current_generation));
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
