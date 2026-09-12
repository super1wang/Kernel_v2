#pragma once
#include <ock/state/domain.hpp>
#include <ock/state/edit.hpp>
#include <functional>

namespace ock::state {
struct ObjectStateProvider {
  using EditPort=ObjectEdit;
  class CandidateReadPort final {
  public:
    explicit CandidateReadPort(const ObjectEdit& edit):edit_(&edit) {}
    std::optional<ObjectRecord> find(foundation::ObjectId id) const {return edit_->find(id);}
    const ObjectRoot& root() const noexcept {return edit_->candidate();}
  private:
    const ObjectEdit* edit_;
  };
  struct Frame final {
    Snapshot<ObjectRoot> base;
    ObjectEdit edit;
    Frame(Snapshot<ObjectRoot> snapshot,ObjectEdit candidate):base(std::move(snapshot)),edit(std::move(candidate)) {}
    CandidateReadPort candidate() const {return CandidateReadPort(edit);}
  };
};
}
namespace ock::contracts {
template<> struct ProviderContract<state::ObjectStateProvider> {
  static AtomicProviderKey key() {return *Name::parse("ock.state.objects");}
};
}
namespace ock::state {
enum class AtomicForm {Call,Await,Ticket,Nested,Conditional,Loop,Parallel};
struct AtomicDescription {
  contracts::OperationKey operation;
  contracts::ContractDigest contract;
  contracts::Shape shape;
  contracts::AtomicMode mode;
  std::optional<contracts::AtomicProviderKey> provider;
  contracts::AtomicDomainRef domain;
  foundation::ObjectId target;
  AtomicForm form=AtomicForm::Call;
  bool complete=true;
  bool async_dispatch=false;
  bool external_wait=false;
};
class AtomicAuthorityPort : public contracts::PortLifetime {
public:
  virtual foundation::Result<void> preflight(const contracts::AtomicDomainRef&,std::uint64_t,
      std::span<const AtomicDescription>,std::span<const foundation::ObjectId> resources) const=0;
  virtual foundation::Result<void> authorize(const AtomicDescription&,contracts::WorkContext&) const=0;
};
struct AtomicOptions {
  std::size_t steps=0;
  std::size_t result_bytes=0;
  std::size_t result_values=0;
  EditOptions edit;
};
class AtomicStep final {
  using Invoke=std::function<foundation::Result<ObjectValue>(ObjectEdit&,contracts::WorkContext&)>;
public:
  template<RootValue A,RootValue R> requires (contracts::ContractValue<A>&&contracts::ContractValue<R>)
  static foundation::Result<AtomicStep> state_edit(AtomicDescription description,const A& input,
      foundation::Result<R>(*handler)(const A&,contracts::EditView<ObjectStateProvider>&,contracts::WorkContext&),
      std::size_t input_bytes,std::size_t result_bytes) {
    auto valid=validate(description,contracts::AtomicMode::StateEdit,contracts::Shape::StateEdit,true);
    if(!valid||!handler)return foundation::make_unexpected(valid?error(StateErrc::InvalidCandidate):valid.error());
    auto frozen=FrozenRoot<A>::freeze(input,input_bytes);if(!frozen)return foundation::make_unexpected(frozen.error());
    try {return AtomicStep{description,[frozen=*frozen,handler,result_bytes,domain=description.domain](ObjectEdit& edit,contracts::WorkContext& work)->foundation::Result<ObjectValue> {
      contracts::EditView<ObjectStateProvider> view(edit,domain);
      auto output=handler(frozen.value(),view,work);if(!output)return foundation::make_unexpected(output.error());
      return ObjectValue::freeze(*output,result_bytes);
    }};} catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  template<RootValue A,RootValue R> requires (contracts::ContractValue<A>&&contracts::ContractValue<R>)
  static foundation::Result<AtomicStep> candidate_read(AtomicDescription description,const A& input,
      foundation::Result<R>(*handler)(const A&,const ObjectStateProvider::CandidateReadPort&,contracts::WorkContext&),
      std::size_t input_bytes,std::size_t result_bytes) {
    auto valid=validate(description,contracts::AtomicMode::CandidateRead,contracts::Shape::Read,true);
    if(!valid||!handler)return foundation::make_unexpected(valid?error(StateErrc::InvalidCandidate):valid.error());
    auto frozen=FrozenRoot<A>::freeze(input,input_bytes);if(!frozen)return foundation::make_unexpected(frozen.error());
    try {return AtomicStep{std::move(description),[frozen=*frozen,handler,result_bytes](ObjectEdit& edit,contracts::WorkContext& work)->foundation::Result<ObjectValue> {
      ObjectStateProvider::CandidateReadPort reader(edit);auto output=handler(frozen.value(),reader,work);
      if(!output)return foundation::make_unexpected(output.error());return ObjectValue::freeze(*output,result_bytes);
    }};} catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  template<RootValue A,RootValue R> requires (contracts::ContractValue<A>&&contracts::ContractValue<R>)
  static foundation::Result<AtomicStep> pure_compute(AtomicDescription description,const A& input,
      foundation::Result<R>(*handler)(const A&,contracts::WorkContext&),std::size_t input_bytes,std::size_t result_bytes) {
    auto valid=validate(description,contracts::AtomicMode::PureCompute,contracts::Shape::Read,false);
    if(!valid||!handler)return foundation::make_unexpected(valid?error(StateErrc::InvalidCandidate):valid.error());
    auto frozen=FrozenRoot<A>::freeze(input,input_bytes);if(!frozen)return foundation::make_unexpected(frozen.error());
    try {return AtomicStep{std::move(description),[frozen=*frozen,handler,result_bytes](ObjectEdit&,contracts::WorkContext& work)->foundation::Result<ObjectValue> {
      auto output=handler(frozen.value(),work);if(!output)return foundation::make_unexpected(output.error());
      return ObjectValue::freeze(*output,result_bytes);
    }};} catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  const AtomicDescription& description() const noexcept {return description_;}
  foundation::Result<ObjectValue> invoke(ObjectEdit& edit,contracts::WorkContext& work) const {return invoke_(edit,work);}
private:
  static foundation::Result<void> validate(const AtomicDescription& d,contracts::AtomicMode mode,
      contracts::Shape shape,bool provider) {
    if(d.form!=AtomicForm::Call||!d.complete||d.async_dispatch||d.external_wait||d.mode!=mode||d.shape!=shape||
        !contracts::valid_domain(d.domain)||d.target.empty()||d.target!=d.domain.domain_id||
        (provider&&(!d.provider||*d.provider!=d.domain.provider))||(!provider&&d.provider))
      return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    return {};
  }
  AtomicStep(AtomicDescription description,Invoke invoke):description_(std::move(description)),invoke_(std::move(invoke)) {}
  AtomicDescription description_;
  Invoke invoke_;
};
struct AtomicExecution {
  std::vector<ObjectValue> values;
  MemoryCommit commit;
};
class ObjectAtomic final {
public:
  ObjectAtomic(std::shared_ptr<StateDomain<ObjectRoot>> domain,std::shared_ptr<const AtomicAuthorityPort> authority,
      AtomicOptions options):domain_(std::move(domain)),authority_(std::move(authority)),options_(options) {}
  foundation::Result<AtomicExecution> execute(const contracts::CallerView& caller,std::span<const AtomicStep> steps,
      std::span<const foundation::ObjectId> resources,const contracts::PreparedIdentity& identity,
      std::shared_ptr<const contracts::ActionPermit> permit,contracts::PermitAuthorityPort& permits,
      const contracts::PermitBinding& binding,contracts::WorkContext& work) const {
    if(!domain_||!authority_||!options_.steps||options_.steps>128||steps.empty()||steps.size()>options_.steps||
        !options_.result_bytes||!options_.result_values||steps.size()>options_.result_values)
      return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    try {
      std::vector<AtomicDescription> descriptions;descriptions.reserve(steps.size());
      for(const auto& step:steps) {
        const auto& d=step.description();
        if(d.domain!=identity.domain)return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
        descriptions.push_back(d);
      }
      auto base=domain_->snapshot(caller);if(!base)return foundation::make_unexpected(base.error());
      if(base->revision()!=identity.base_revision||base->lifecycle_generation()!=identity.lifecycle_generation)
        return foundation::make_unexpected(error(StateErrc::RevisionConflict));
      auto preflight=authority_->preflight(identity.domain,identity.base_revision,descriptions,resources);
      if(!preflight)return foundation::make_unexpected(preflight.error());
      auto edit=ObjectEdit::begin(base->value(),options_.edit);if(!edit)return foundation::make_unexpected(edit.error());
      std::vector<ObjectValue> values;values.reserve(steps.size());std::size_t bytes=0;
      bool edited=false;
      for(const auto& step:steps) {
        if(work.stop_requested()||work.deadline()<=std::chrono::steady_clock::now())
          return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
        auto current=authority_->authorize(step.description(),work);if(!current)return foundation::make_unexpected(current.error());
        auto value=step.invoke(*edit,work);if(!value)return foundation::make_unexpected(value.error());
        if(value->owned_bytes()>options_.result_bytes-bytes)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
        bytes+=value->owned_bytes();values.push_back(std::move(*value));
        edited|=step.description().mode==contracts::AtomicMode::StateEdit;
      }
      if(!edited)return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
      auto candidate=edit->freeze();if(!candidate)return foundation::make_unexpected(candidate.error());
      auto prepared=domain_->prepare(*base,candidate->value(),identity,edit->delta_bytes());
      if(!prepared)return foundation::make_unexpected(prepared.error());
      auto committed=domain_->commit(*prepared,std::move(permit),permits,binding);
      if(!committed)return foundation::make_unexpected(committed.error());
      return AtomicExecution{std::move(values),std::move(*committed)};
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
    catch(...) {return foundation::make_unexpected(error(StateErrc::InvalidCandidate));}
  }
private:
  std::shared_ptr<StateDomain<ObjectRoot>> domain_;
  std::shared_ptr<const AtomicAuthorityPort> authority_;
  AtomicOptions options_;
};
}
