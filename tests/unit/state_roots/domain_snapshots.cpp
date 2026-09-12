#include "tests/compile/contracts/test_support.hpp"
#include <ock/contracts/ports.hpp>
#include <ock/state/domain.hpp>
#include <functional>
struct CallerIssuer final:CallerAuthorityPort {
  struct Grant final:CallerGrant {
    CallerDescription value;bool active=true;
    explicit Grant(CallerDescription v):value(std::move(v)) {}
    const CallerDescription& description() const noexcept override {return value;}
  };
  PrincipalRef principal{};
  std::vector<std::shared_ptr<Grant>> issued;
  void enter_authenticated(PrincipalRef value) {principal=value;}
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription& request) override {
    if(request.principal!=principal)return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto value=std::make_shared<Grant>(request);issued.push_back(value);
    return std::shared_ptr<const CallerGrant>(value);
  }
  Result<void> validate(const CallerGrant& value) const override {
    for(const auto& row:issued)if(row.get()==&value&&row->active)return {};
    return reject(ContractsErrc::InvalidGrant);
  }
  void revoke(const CallerGrant& value) {for(auto& row:issued)if(row.get()==&value)row->active=false;}
};
struct Configuration {std::vector<int> values;};
template<> struct ock::state::RootContract<Configuration> {
  static Result<Configuration> freeze(const Configuration& value) {return Configuration{value.values};}
  static Result<std::size_t> bytes(const Configuration& value) {return sizeof(value)+value.values.capacity()*sizeof(int);}
};
struct ReadAuthority final:ock::state::SnapshotAuthority {
  std::shared_ptr<CallerAuthorityPort> expected;
  mutable std::function<void()> callback;
  explicit ReadAuthority(std::shared_ptr<CallerAuthorityPort> value):expected(std::move(value)) {}
  Result<void> authorize(const CallerView& caller,const AtomicDomainRef& target) const override {
    if(callback)callback();
    if(target!=domain())return reject(ContractsErrc::InvalidGrant);
    return validate_caller(*expected,caller);
  }
};
int main() try {
  auto issuer=std::make_shared<CallerIssuer>();
  PrincipalRef principal{id<PrincipalId>()};issuer->enter_authenticated(principal);
  auto grant=issuer->authenticate({principal,{},{}});CHECK(grant);
  auto caller=CallerView::check(issuer,*grant);CHECK(caller);
  auto authority=std::make_shared<ReadAuthority>(issuer);
  using Domain=ock::state::StateDomain<Configuration>;
  auto made=Domain::create(domain(),Configuration{{4,5}}, {4096,1},authority);CHECK(made);
  auto owner=*made;
  std::optional<ock::state::Snapshot<Configuration>> pin;
  {auto first=owner->snapshot(*caller);CHECK(first);pin=*first;CHECK(owner->validate_base(*first));}
  CHECK(owner->snapshot_pins()==1&&!owner->snapshot(*caller));
  auto copy=*pin;pin.reset();CHECK(owner->snapshot_pins()==1);
  auto other=Domain::create(domain(),Configuration{{6}}, {4096,1},authority);CHECK(other);
  CHECK(!(*other)->validate_base(copy)); // 同 DTO 不能冒充同一真实域 owner。
  issuer->revoke(**grant);CHECK(!owner->snapshot(*caller));CHECK(copy.value().values[0]==4);
  owner->close();CHECK(!owner->validate_base(copy));CHECK(copy.value().values[0]==4);
  made->reset();owner.reset();CHECK(copy.value().values[1]==5);

  auto fresh_grant=issuer->authenticate({principal,{},{}});CHECK(fresh_grant);
  auto fresh=CallerView::check(issuer,*fresh_grant);CHECK(fresh);
  auto active=Domain::create(domain(),Configuration{{7}}, {4096,2},authority);CHECK(active);
  std::weak_ptr<Domain> weak=*active;
  authority->callback=[&]{(*active)->close();active->reset();};
  auto closed=(*active)->snapshot(*fresh);CHECK(!closed&&weak.expired());
  authority->callback={};
  return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
