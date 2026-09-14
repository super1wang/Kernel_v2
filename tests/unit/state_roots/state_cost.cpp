#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/edit.hpp>
#include <ock/state/domain.hpp>
#include <chrono>

struct CostItem{std::array<std::uint64_t,8> words;};
template<>struct ock::state::RootContract<CostItem>{
  static Result<CostItem> freeze(const CostItem& value){return value;}
  static Result<std::size_t> bytes(const CostItem&){return sizeof(CostItem);}
};
template<>struct ock::contracts::TypeContract<CostItem>{
  static TypeIdentity identity(){return{name("state.cost-item"),ver(),{}};}
  static Result<void> validate(const CostItem&){return{};}
};
static foundation::ObjectId object_id(std::uint64_t value){
  foundation::ObjectId result{};for(unsigned i=0;i<8;++i)result.bytes[15-i]=static_cast<std::uint8_t>(value>>(i*8));return result;
}

struct CostAuthority final:CallerAuthorityPort,ock::state::SnapshotAuthority {
  struct Grant final:CallerGrant {
    CallerDescription value{PrincipalRef{id<PrincipalId>()},{},{}};
    const CallerDescription& description() const noexcept override{return value;}
  };
  std::shared_ptr<Grant> grant=std::make_shared<Grant>();
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription&) override{return std::shared_ptr<const CallerGrant>(grant);}
  Result<void> validate(const CallerGrant& value) const override{return &value==grant.get()?Result<void>{}:reject(ContractsErrc::InvalidGrant);}
  Result<void> authorize(const CallerView& value,const AtomicDomainRef&) const override{return validate_caller(*this,value);}
};
struct CostPermit final:ActionPermit {PermitBinding value;explicit CostPermit(PermitBinding binding):value(std::move(binding)){}const PermitBinding& binding() const noexcept override{return value;}};
struct CostPermits final:PermitAuthorityPort {
  bool used=false;
  Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding&) override{return make_unexpected(error(ContractsErrc::InvalidAuthority));}
  Result<void> consume(const ActionPermit&,const PermitBinding&) override{return reject(ContractsErrc::InvalidAuthority);}
  Result<void> consume_claimed(const ActionPermit& permit,const PermitBinding& expected,CommitClaim& claim) override {
    if(used||permit.binding()!=expected||!claim.try_claim())return reject(ContractsErrc::InvalidGrant);used=true;return {};
  }
};
using Clock=std::chrono::steady_clock;
static auto ns(Clock::time_point from,Clock::time_point to){return std::chrono::duration_cast<std::chrono::nanoseconds>(to-from).count();}
int main()try {
  using namespace ock::state;constexpr std::size_t limit=64*1024*1024;
  auto authority=std::make_shared<CostAuthority>();auto caller=CallerView::check(authority,authority->grant);CHECK(caller);
  std::size_t cases=0;
  for(bool references:{false,true})for(std::size_t count:{1000,10000,100000}) {
    ObjectRoot root;bool capacity=false;
    for(std::size_t i=0;i<count;++i) {
      auto value=ObjectValue::freeze(CostItem{{i,i,i,i,i,i,i,i}},1024);CHECK(value);
      std::array<foundation::ObjectId,1> ref{object_id(i)};
      auto record=ObjectRecord::create(object_id(i+1),*value,references&&i?std::span<const foundation::ObjectId>(ref):std::span<const foundation::ObjectId>{},1);CHECK(record);
      auto next=root.replace(*record,limit);
      if(!next){CHECK(next.error().code()==ock::state::error(StateErrc::BudgetExceeded).code());capacity=true;break;}
      root=std::move(*next);
    }
    auto baseline=root.index_memory();
    for(std::size_t changed:{1,10,100}) {
      ++cases;
      if(capacity) {std::cout<<"{\"objects\":"<<count<<",\"changed\":"<<changed<<",\"references\":"<<references<<",\"status\":\"BudgetRefused\",\"stage\":\"build\",\"constructed\":"<<root.size()<<"}\n";continue;}
      DomainOptions options{limit,limit,4096,8,limit,8,8,1,2};
      auto made=StateDomain<ObjectRoot>::create(domain(),root,options,authority);CHECK(made);
      auto state=*made;made->reset();
      const auto memory_before=root.index_memory();
      Clock::time_point begin,snapped,edited,validated,prepared_time,committed_time;
      std::size_t delta=0,ring=0,retained=0;IndexMemory live{};
      {
        begin=Clock::now();auto snapshot=state->snapshot(*caller);CHECK(snapshot);snapped=Clock::now();
        auto edit=ObjectEdit::begin(snapshot->value(),{limit,limit,128});CHECK(edit);
        for(std::size_t i=0;i<changed;++i) {
          auto object=object_id(count-i);auto prior=edit->find(object);CHECK(prior);
          auto value=ObjectValue::freeze(CostItem{{9,9,9,9,9,9,9,9}},1024);CHECK(value);
          auto record=ObjectRecord::create(object,*value,prior->references(),1);CHECK(record);CHECK(edit->replace(*record));
        }
        edited=Clock::now();CHECK(edit->candidate().validate_references());validated=Clock::now();
        PreparedIdentity identity{id<CommitId>(),id<ReservationId>(),domain(),0,1};
        auto prepared=state->prepare(*snapshot,edit->candidate(),identity,edit->delta_bytes());CHECK(prepared);prepared_time=Clock::now();
        PermitBinding binding{PrincipalRef{id<PrincipalId>()},key(),{},domain().domain_id,1,1,Clock::now()+std::chrono::seconds(10)};
        CostPermits permits;auto result=state->commit(*prepared,std::make_shared<CostPermit>(binding),permits,binding);CHECK(result);committed_time=Clock::now();
        CHECK(result->publication.revision==1&&state->snapshot(*caller)->revision()==1);
        delta=edit->delta_bytes();ring=state->history_ring_bytes();retained=state->history_retained_bytes();live=root.index_memory();
        CHECK(snapshot->value().size()==count&&snapshot->value().find(object_id(1))->value().get<CostItem>()==root.find(object_id(1))->value().get<CostItem>());
      }
      auto reclaim_begin=Clock::now();state->close();state.reset();auto reclaimed=Clock::now();
      auto after=root.index_memory();CHECK(after.retained_bytes==baseline.retained_bytes);
      std::cout<<"{\"objects\":"<<count<<",\"changed\":"<<changed<<",\"references\":"<<references<<",\"status\":\"Passed\""
        <<",\"snapshot_ns\":"<<ns(begin,snapped)<<",\"edit_ns\":"<<ns(snapped,edited)<<",\"constraints_ns\":"<<ns(edited,validated)
        <<",\"prepare_ns\":"<<ns(validated,prepared_time)<<",\"commit_ns\":"<<ns(prepared_time,committed_time)<<",\"reclaim_ns\":"<<ns(reclaim_begin,reclaimed)
        <<",\"logical_root_bytes\":"<<root.logical_bytes()<<",\"logical_delta_bytes\":"<<delta<<",\"history_ring_bytes\":"<<ring<<",\"history_retained_bytes\":"<<retained
        <<",\"index_unique_retained_bytes\":"<<live.retained_bytes<<",\"index_allocated_bytes\":"<<live.allocated_bytes-memory_before.allocated_bytes
        <<",\"index_allocations\":"<<live.allocations-memory_before.allocations<<",\"index_after_reclaim_bytes\":"<<after.retained_bytes<<",\"baseline_index_bytes\":"<<baseline.retained_bytes<<"}\n";
    }
  }
  CHECK(cases==18);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
