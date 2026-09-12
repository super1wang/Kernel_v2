#include <ock/state/edit.hpp>
#include <ock/state/domain.hpp>
#include <ock/foundation/sdk_version.hpp>
#include <iostream>

using namespace ock::contracts;
namespace foundation=ock::foundation;
template<class T>T id(unsigned n=1){T value{};value.bytes[15]=static_cast<std::uint8_t>(n);return value;}
Name name(const char* value){return *Name::parse(value);}
OperationVersion version(){return *OperationVersion::parse("1.0.0",5);}
AtomicDomainRef domain(){return{*Name::parse("installed.state"),id<foundation::ObjectId>(),id<foundation::RegistryGeneration>()};}
struct Setting{int value;};
namespace ock::contracts {template<>struct TypeContract<Setting>{
  static TypeIdentity identity(){return{*Name::parse("installed.setting"),::version(),{}};}
  static Result<void> validate(const Setting&){return{};}
};}
namespace ock::state {template<>struct RootContract<Setting>{
  static foundation::Result<Setting> freeze(const Setting& value){return value;}
  static foundation::Result<std::size_t> bytes(const Setting&){return sizeof(Setting);}
};}
struct Issuer final:CallerAuthorityPort {
  struct Grant final:CallerGrant{CallerDescription value;explicit Grant(CallerDescription v):value(std::move(v)){}
    const CallerDescription& description()const noexcept override{return value;}};
  std::shared_ptr<Grant> current;
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription& value)override{
    current=std::make_shared<Grant>(value);return std::shared_ptr<const CallerGrant>(current);}
  Result<void> validate(const CallerGrant& value)const override{return current.get()==&value?Result<void>{}:reject(ContractsErrc::InvalidGrant);}
};
struct Reads final:ock::state::SnapshotAuthority{
  std::shared_ptr<Issuer> issuer;explicit Reads(std::shared_ptr<Issuer> v):issuer(std::move(v)){}
  Result<void> authorize(const CallerView& caller,const AtomicDomainRef& value)const override{
    if(value!=domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*issuer,caller);}
};
struct Permit final:ActionPermit{PermitBinding value;explicit Permit(PermitBinding v):value(std::move(v)){}
  const PermitBinding& binding()const noexcept override{return value;}};
struct Permits final:PermitAuthorityPort{
  Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding& value)override{
    return std::shared_ptr<const ActionPermit>(std::make_shared<Permit>(value));}
  Result<void> consume(const ActionPermit& permit,const PermitBinding& expected)override{
    return permit.binding()==expected?Result<void>{}:reject(ContractsErrc::InvalidGrant);}
};
int main(){
  auto issuer=std::make_shared<Issuer>();PrincipalRef principal{id<PrincipalId>()};
  auto grant=issuer->authenticate({principal,{},{}});if(!grant)return 1;
  auto caller=CallerView::check(issuer,*grant);if(!caller)return 2;
  ock::state::DomainOptions limits{4096,4096,512,4,4096,4,2,1,1};
  auto state=ock::state::StateDomain<ock::state::ObjectRoot>::create(domain(),{},limits,std::make_shared<Reads>(issuer));if(!state)return 3;
  auto base=(*state)->snapshot(*caller);if(!base)return 4;
  auto value=ock::state::ObjectValue::freeze(Setting{7},512);if(!value)return 5;
  auto object=ock::state::ObjectRecord::create(id<foundation::ObjectId>(2),*value,{},4);if(!object)return 6;
  auto edit=ock::state::ObjectEdit::begin(base->value(),{4096,4096,8});if(!edit||!edit->create(*object))return 7;
  PreparedIdentity identity{id<CommitId>(),id<ReservationId>(),domain(),0,1};
  auto prepared=(*state)->prepare(*base,edit->candidate(),identity,edit->delta_bytes());if(!prepared)return 8;
  OperationKey operation{name("installed.set"),version()};ContractDigest digest{};
  PermitBinding binding{principal,operation,digest,domain().domain_id,1,1,std::chrono::steady_clock::now()+std::chrono::minutes(1)};
  Permits permits;auto committed=(*state)->commit(*prepared,std::make_shared<Permit>(binding),permits,binding);if(!committed)return 9;
  auto snapshot=(*state)->snapshot(*caller);if(!snapshot||snapshot->revision()!=1)return 10;
  auto row=snapshot->value().find(id<foundation::ObjectId>(2));if(!row||row->value().get<Setting>()->value!=7)return 11;
  std::cout<<ock::sdk::version<<" State installed snapshot/commit passed\n";return 0;
}
