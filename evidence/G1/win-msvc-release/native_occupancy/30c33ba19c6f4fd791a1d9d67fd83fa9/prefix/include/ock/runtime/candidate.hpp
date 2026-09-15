#pragma once
#include <ock/contracts/candidate.hpp>
#include <ock/runtime/registry.hpp>
#include <ock/runtime/native_types.hpp>

namespace ock::runtime::registry {
template<class P> class CandidateStep final {
public:
  const std::shared_ptr<const contracts::CandidateCallPort<P>>& port() const noexcept{return port_;}
  std::span<const ResourceRef> resources() const noexcept{return resources_;}
  const std::shared_ptr<const void>& provider_owner() const noexcept{return provider_owner_;}
  const std::shared_ptr<const Catalog>& catalog() const noexcept{return catalog_;}
private:
  friend class CandidateBindings;
  CandidateStep(std::shared_ptr<const Catalog> catalog,std::shared_ptr<const contracts::CandidateCallPort<P>> port,
      std::vector<ResourceRef> resources,std::shared_ptr<const void> provider):catalog_(std::move(catalog)),port_(std::move(port)),resources_(std::move(resources)),provider_owner_(std::move(provider)) {}
  std::shared_ptr<const Catalog> catalog_;
  std::shared_ptr<const contracts::CandidateCallPort<P>> port_;
  std::vector<ResourceRef> resources_;
  std::shared_ptr<const void> provider_owner_;
};
// The only public factory takes an actual frozen registration, never a handler.
class CandidateBindings final {
public:
  template<class P,AsyncInput A,AsyncInput R,class Reader=void>
  static Result<CandidateStep<P>> bind(std::shared_ptr<const Catalog> catalog,const OperationKey& key,
      ContractDigest digest,AtomicMode mode,A input,AtomicDomainRef domain,
      std::span<const foundation::ObjectId> targets,invocation::TargetProjection<A> projection,std::optional<std::size_t> input_slot={}) {
    if((input_slot&&*input_slot>=128)||!catalog||!projection||targets.empty()||targets.size()>64||!valid_domain(domain))
      return make_unexpected(error(ContractsErrc::InvalidContract));
    try {
      auto handle=catalog->find(key);if(!handle)return make_unexpected(handle.error());
      auto index=foundation::resolve_slot(*handle,catalog->identity(),catalog->generation(),catalog->size());
      if(!index)return make_unexpected(index.error());
      const auto& entry=catalog->hot_[*index];const auto& definition=catalog->cold_[*index];
      const auto& description=definition->description();
      if(description.contract_digest!=digest||description.atomic_mode!=mode||
          definition->args_type()!=CppTypeToken::of<A>()||definition->result_type()!=CppTypeToken::of<R>()||
          entry.async_dispatch||entry.external_wait||definition->asynchronous_read()||
          entry.submission_storage_type!=CppTypeToken::of<SubmissionStorage<A,R>>()||!entry.submission_storage)
        return make_unexpected(error(ContractsErrc::InvalidContract));
      auto storage=std::static_pointer_cast<const SubmissionStorage<A,R>>(entry.submission_storage);
      if(!storage->state_result_bytes||storage->atomic_membership)return make_unexpected(error(ContractsErrc::InvalidContract));
      auto valid=TypeContract<A>::validate(input);if(!valid)return make_unexpected(valid.error());
      auto bytes=storage->input_bytes(input);if(!bytes)return make_unexpected(bytes.error());
      if(*bytes>storage->input_limit)return make_unexpected(error(ContractsErrc::BudgetExceeded));
      auto frozen=std::make_shared<const A>(std::move(input));
      std::function<Result<R>(const A&,typename P::EditPort&,WorkContext&)> invoke;
      if(mode==AtomicMode::StateEdit) {
        using F=Result<R>(*)(const A&,EditView<P>&,WorkContext&);
        if(entry.handler_type!=CppTypeToken::of<F>()||entry.provider_type!=CppTypeToken::of<P>())
          return make_unexpected(error(ContractsErrc::TypeMismatch));
        auto handler=std::static_pointer_cast<const F>(entry.handler);
        invoke=[handler,domain](const A& input,typename P::EditPort& edit,WorkContext& work)->Result<R>{
          EditView<P> view(edit,domain);return (*handler)(input,view,work);
        };
      } else if(mode==AtomicMode::PureCompute) {
        using F=Result<R>(*)(const A&,WorkContext&);
        if(entry.handler_type!=CppTypeToken::of<F>()||definition->provider())return make_unexpected(error(ContractsErrc::TypeMismatch));
        auto handler=std::static_pointer_cast<const F>(entry.handler);
        invoke=[handler](const A& input,typename P::EditPort&,WorkContext& work)->Result<R>{return (*handler)(input,work);};
      } else if(mode==AtomicMode::CandidateRead) {
        if constexpr(std::same_as<Reader,void>)return make_unexpected(error(ContractsErrc::TypeMismatch));
        else {
          using F=std::pair<Result<R>(*)(const A&,WorkContext&,ReadServices<Reader>&),
              Result<R>(*)(const A&,const typename P::CandidateReadPort&,WorkContext&)>;
          if(entry.handler_type!=CppTypeToken::of<F>()||entry.provider_type!=CppTypeToken::of<P>())
            return make_unexpected(error(ContractsErrc::TypeMismatch));
          auto handler=std::static_pointer_cast<const F>(entry.handler);
          invoke=[handler](const A& input,typename P::EditPort& edit,WorkContext& work)->Result<R>{
            typename P::CandidateReadPort reader(edit);return handler->second(input,reader,work);
          };
        }
      } else return make_unexpected(error(ContractsErrc::InvalidContract));
      if(mode!=AtomicMode::PureCompute) {
        auto provider=std::static_pointer_cast<AtomicProviderPort<P>>(entry.provider_owner);
        if(!provider)return make_unexpected(error(ContractsErrc::InvalidContract));
        auto resolved=provider->resolve(domain.domain_id);
        if(!resolved||*resolved!=domain)return make_unexpected(error(ContractsErrc::InvalidContract));
      }
      class Call final:public CandidateCallPort<P> {
      public:
        Call(std::shared_ptr<const Catalog> owner,std::shared_ptr<const DefinitionSnapshot> definition,
            std::shared_ptr<const A> input,std::shared_ptr<const SubmissionStorage<A,R>> storage,
            AtomicDomainRef domain,std::vector<foundation::ObjectId> targets,invocation::TargetProjection<A> projection,
            std::function<Result<R>(const A&,typename P::EditPort&,WorkContext&)> invoke,std::optional<std::size_t> input_slot)
            :owner_(std::move(owner)),definition_(std::move(definition)),input_(std::move(input)),storage_(std::move(storage)),
             domain_(std::move(domain)),targets_(std::move(targets)),projection_(projection),invoke_(std::move(invoke)),input_slot_(input_slot) {}
        const OperationKey& operation() const noexcept override{return definition_->description().key;}
        ContractDigest contract() const noexcept override{return definition_->description().contract_digest;}
        AtomicMode mode() const noexcept override{return definition_->description().atomic_mode;}
        const AtomicDomainRef& domain() const noexcept override{return domain_;}
        std::span<const foundation::ObjectId> targets() const noexcept override{return targets_;}
        Result<std::size_t> input_bytes() const override {
          auto bytes=storage_->input_bytes(*input_);if(!bytes)return make_unexpected(bytes.error());
          const auto overhead=sizeof(Call)+targets_.capacity()*sizeof(foundation::ObjectId)+8*sizeof(void*);
          if(*bytes>std::numeric_limits<std::size_t>::max()-overhead)return make_unexpected(error(ContractsErrc::BudgetExceeded));
          return *bytes+overhead;
        }
        std::optional<std::size_t> input_slot() const noexcept override{return input_slot_;}
        CppTypeToken input_type() const noexcept override{return CppTypeToken::of<A>();}
        CppTypeToken result_type() const noexcept override{return CppTypeToken::of<R>();}
        Result<void> validate() const override{return validate_input(*input_);}
        Result<void> validate_input(const A& input) const {
          auto valid=TypeContract<A>::validate(input);if(!valid)return valid;
          auto bytes=storage_->input_bytes(input);if(!bytes)return make_unexpected(bytes.error());
          if(*bytes>storage_->input_limit)return reject(ContractsErrc::BudgetExceeded);
          std::array<foundation::ObjectId,64> projected{};auto count=projection_(input,projected);
          if(!count)return make_unexpected(count.error());
          if(*count!=targets_.size()||*count>projected.size()||
              !std::equal(targets_.begin(),targets_.end(),projected.begin()))return reject(ContractsErrc::InvalidContract);
          return {};
        }
        Result<CandidateValue> invoke(typename P::EditPort& edit,WorkContext& work,std::span<const CandidateValue> previous={}) const override {
          const A* input=input_.get();
          if(input_slot_) {
            if(*input_slot_>=previous.size()||!(input=previous[*input_slot_].template get<A>()))return make_unexpected(error(ContractsErrc::TypeMismatch));
          }
          auto valid=validate_input(*input);if(!valid)return make_unexpected(valid.error());
          auto result=invoke_(*input,edit,work);if(!result)return make_unexpected(result.error());
          auto bytes=storage_->state_result_bytes(*result);if(!bytes)return make_unexpected(bytes.error());
          if(*bytes>storage_->reply_limit)return make_unexpected(error(ContractsErrc::BudgetExceeded));
          return CandidateValue::create(std::move(*result),*bytes);
        }
      private:
        std::shared_ptr<const Catalog> owner_;std::shared_ptr<const DefinitionSnapshot> definition_;
        std::shared_ptr<const A> input_;std::shared_ptr<const SubmissionStorage<A,R>> storage_;
        AtomicDomainRef domain_;std::vector<foundation::ObjectId> targets_;invocation::TargetProjection<A> projection_;
        std::function<Result<R>(const A&,typename P::EditPort&,WorkContext&)> invoke_;
        std::optional<std::size_t> input_slot_;
      };
      auto call=std::make_shared<const Call>(catalog,definition,std::move(frozen),storage,domain,
          std::vector<foundation::ObjectId>(targets.begin(),targets.end()),projection,std::move(invoke),input_slot);
      auto checked=call->validate();if(!checked)return make_unexpected(checked.error());
      return CandidateStep<P>{std::move(catalog),std::move(call),entry.resources,entry.provider_owner};
    } catch(const std::bad_alloc&) {return make_unexpected(error(ContractsErrc::BudgetExceeded));}
    catch(...) {return make_unexpected(error(ContractsErrc::InvalidContract));}
  }
};
}
