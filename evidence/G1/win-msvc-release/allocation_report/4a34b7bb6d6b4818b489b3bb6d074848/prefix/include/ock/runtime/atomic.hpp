#pragma once
#include <ock/runtime/candidate.hpp>

namespace ock::runtime::atomic {
using namespace contracts;
class Assertion final {
public:
  template<std::integral R> static Assertion equals(std::size_t slot,R expected) {
    return Assertion{slot,[expected](const CandidateValue& value){auto actual=value.get<R>();return actual&&*actual==expected;}};
  }
  bool check(std::span<const CandidateValue> values) const{return slot_<values.size()&&check_(values[slot_]);}
  std::size_t slot() const noexcept{return slot_;}
private:
  Assertion(std::size_t slot,std::function<bool(const CandidateValue&)> check):slot_(slot),check_(std::move(check)) {}
  std::size_t slot_;std::function<bool(const CandidateValue&)> check_;
};
struct Results {std::vector<CandidateValue> values;};
template<class P> class Input final {
public:
  static Result<Input> create(std::vector<registry::CandidateStep<P>> steps,std::vector<Assertion> assertions={},
      std::size_t result_bytes=64*1024*1024) try {
    if(steps.empty()||steps.size()>128||assertions.size()>128||!result_bytes||result_bytes>64*1024*1024)
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    Input input{std::make_shared<const Storage>(Storage{std::move(steps),std::move(assertions),result_bytes})};
    auto valid=input.validate();if(!valid)return make_unexpected(valid.error());return input;
  } catch(const std::bad_alloc&) {return make_unexpected(error(ContractsErrc::BudgetExceeded));}
    catch(...) {return make_unexpected(error(ContractsErrc::InvalidContract));}
  Result<void> validate() const {
    if(!storage_||storage_->steps.empty()||storage_->steps.size()>128)return reject(ContractsErrc::InvalidContract);
    const auto& first=storage_->steps.front();bool edited=false;std::shared_ptr<const void> provider;
    std::size_t index=0;
    for(const auto& step:storage_->steps) {
      if(auto slot=step.port()->input_slot();slot&&(*slot>=index||storage_->steps[*slot].port()->result_type()!=step.port()->input_type()))return reject(ContractsErrc::TypeMismatch);
      ++index;
      if(step.catalog()!=first.catalog()||step.port()->domain()!=first.port()->domain())return reject(ContractsErrc::InvalidContract);
      auto valid=step.port()->validate();if(!valid)return valid;
      if(step.port()->mode()!=AtomicMode::PureCompute) {
        if(!step.provider_owner()||(provider&&provider.get()!=step.provider_owner().get()))return reject(ContractsErrc::InvalidAuthority);
        provider=step.provider_owner();
      }
      edited|=step.port()->mode()==AtomicMode::StateEdit;
    }
    for(const auto& assertion:storage_->assertions)if(assertion.slot()>=storage_->steps.size())return reject(ContractsErrc::InvalidContract);
    return edited?Result<void>{}:reject(ContractsErrc::InvalidContract);
  }
  const AtomicDomainRef& domain() const noexcept{return storage_->steps.front().port()->domain();}
  Result<registry::AtomicMembership> membership() const {
    auto valid=validate();if(!valid)return make_unexpected(valid.error());
    auto catalog=storage_->steps.front().catalog();registry::AtomicMembership result{catalog->identity(),catalog->generation(),{}, {}};
    for(const auto& step:storage_->steps) {
      if(step.provider_owner())result.provider_owner=step.provider_owner();
      auto port=step.port();result.members.push_back({port->operation(),port->contract(),{port->targets().begin(),port->targets().end()}});
      for(const auto& resource:step.resources())
        if(std::find(result.resources.begin(),result.resources.end(),resource)==result.resources.end())result.resources.push_back(resource);
    }
    return result;
  }
  Result<std::size_t> bytes() const {
    constexpr std::size_t limit=64*1024*1024;
    std::size_t total=sizeof(Input)+sizeof(Storage);
    if(storage_->steps.capacity()>(limit-total)/sizeof(registry::CandidateStep<P>))return make_unexpected(error(ContractsErrc::BudgetExceeded));
    total+=storage_->steps.capacity()*sizeof(registry::CandidateStep<P>);
    if(storage_->assertions.capacity()>(limit-total)/sizeof(Assertion))return make_unexpected(error(ContractsErrc::BudgetExceeded));
    total+=storage_->assertions.capacity()*sizeof(Assertion);
    for(const auto& step:storage_->steps) {
      auto count=step.port()->input_bytes();if(!count)return make_unexpected(count.error());
      if(step.resources().size()>(limit-total)/sizeof(registry::ResourceRef))return make_unexpected(error(ContractsErrc::BudgetExceeded));
      const auto resources=step.resources().size()*sizeof(registry::ResourceRef);
      if(resources>64*1024*1024-total)return make_unexpected(error(ContractsErrc::BudgetExceeded));total+=resources;
      if(*count>64*1024*1024-total)return make_unexpected(error(ContractsErrc::BudgetExceeded));total+=*count;
    }
    return total;
  }
  static Result<Results> apply(const Input& input,EditView<P>& view,WorkContext& work) {
    auto valid=input.validate();if(!valid)return make_unexpected(valid.error());
    if(view.domain()!=input.domain())return make_unexpected(error(ContractsErrc::InvalidContract));
    Results output;output.values.reserve(input.storage_->steps.size());std::size_t bytes=0;
    for(const auto& step:input.storage_->steps) {
      if(work.stop_requested())return make_unexpected(error(ContractsErrc::Rejected));
      auto current=work.validate_candidate_authorization();if(!current)return make_unexpected(current.error());
      auto value=step.port()->invoke(view.edit(),work,output.values);if(!value)return make_unexpected(value.error());
      if(value->bytes()>input.storage_->result_bytes-bytes)return make_unexpected(error(ContractsErrc::BudgetExceeded));
      bytes+=value->bytes();output.values.push_back(std::move(*value));
    }
    for(const auto& assertion:input.storage_->assertions)if(!assertion.check(output.values))return make_unexpected(error(ContractsErrc::Rejected));
    return output;
  }
private:
  struct Storage {std::vector<registry::CandidateStep<P>> steps;std::vector<Assertion> assertions;std::size_t result_bytes;};
  explicit Input(std::shared_ptr<const Storage> storage):storage_(std::move(storage)) {}
  std::shared_ptr<const Storage> storage_;
};
}
namespace ock::contracts {
template<class P> struct TypeContract<runtime::atomic::Input<P>> {
  static constexpr AsyncOwnership async_ownership=AsyncOwnership::Owning;
  static TypeIdentity identity(){return {*Name::parse("ock.atomic.input"),*OperationVersion::parse("1.0.0",5),{}};}
  static Result<void> validate(const runtime::atomic::Input<P>& value){return value.validate();}
};
template<> struct TypeContract<runtime::atomic::Results> {
  static constexpr AsyncOwnership async_ownership=AsyncOwnership::Owning;
  static TypeIdentity identity(){return {*Name::parse("ock.atomic.results"),*OperationVersion::parse("1.0.0",5),{}};}
  static Result<void> validate(const runtime::atomic::Results& value){return value.values.size()<=128?Result<void>{}:reject(ContractsErrc::BudgetExceeded);}
};
}

namespace ock::runtime::atomic {
inline Result<std::size_t> result_bytes(const Results& result) {
  std::size_t bytes=sizeof(Results)+result.values.capacity()*sizeof(CandidateValue);
  if(bytes>64*1024*1024)return make_unexpected(error(ContractsErrc::BudgetExceeded));
  for(const auto& value:result.values) {
    if(value.bytes()>64*1024*1024-bytes)return make_unexpected(error(ContractsErrc::BudgetExceeded));bytes+=value.bytes();
  }
  return bytes;
}
template<class P> Result<void> register_group(registry::Registrar& registrar,const DefinitionInput& definition,
    const registry::OperationOptions& options,std::size_t input_limit=1024*1024,std::size_t reply_limit=1024*1024) {
  if(!input_limit||!reply_limit||input_limit>64*1024*1024||reply_limit>64*1024*1024)return reject(ContractsErrc::BudgetExceeded);
  registry::SubmissionStorage<Input<P>,Results> storage{input_limit,reply_limit,
      [](const Input<P>& input){return input.bytes();},
      [](const InvokeReply<Results>& reply)->Result<std::size_t>{
        if(auto completed=std::get_if<Completed<Results>>(&reply))
          if(auto committed=std::get_if<StateCommitted<Results>>(&completed->outcome.value());committed&&committed->result) {
            auto bytes=result_bytes(*committed->result);if(!bytes)return make_unexpected(bytes.error());
            if(*bytes>64*1024*1024-sizeof(reply))return make_unexpected(error(ContractsErrc::BudgetExceeded));return *bytes+sizeof(reply);
          }
        return sizeof(reply);
      },result_bytes,[](const Input<P>& input){return input.membership();}};
  return registrar.state_edit(&Input<P>::apply,definition,options,storage);
}
template<class P> Result<std::size_t> target(const Input<P>& input,std::span<foundation::ObjectId> output) noexcept {
  if(output.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));output[0]=input.domain().domain_id;return 1;
}
}
