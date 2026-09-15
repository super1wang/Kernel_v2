#pragma once
#include <ock/contracts/operation.hpp>

namespace ock::contracts {
// Owning, validated result of one bounded candidate call. This is not a Plan IR.
class CandidateValue final {
public:
  template<AsyncInput R> static Result<CandidateValue> create(R value,std::size_t bytes) {
    auto valid=TypeContract<R>::validate(value);if(!valid)return make_unexpected(valid.error());
    return CandidateValue{std::make_shared<const R>(std::move(value)),CppTypeToken::of<R>(),TypeContract<R>::identity(),bytes};
  }
  template<ContractValue R> const R* get() const noexcept {
    return token_==CppTypeToken::of<R>()?static_cast<const R*>(value_.get()):nullptr;
  }
  const TypeIdentity& type() const noexcept{return type_;}
  CppTypeToken token() const noexcept{return token_;}
  std::size_t bytes() const noexcept{return bytes_;}
private:
  CandidateValue(std::shared_ptr<const void> value,CppTypeToken token,TypeIdentity type,std::size_t bytes)
      :value_(std::move(value)),token_(token),type_(std::move(type)),bytes_(bytes) {}
  std::shared_ptr<const void> value_;CppTypeToken token_;TypeIdentity type_;std::size_t bytes_;
};
template<class P> class CandidateCallPort:public PortLifetime {
public:
  virtual const OperationKey& operation() const noexcept=0;
  virtual ContractDigest contract() const noexcept=0;
  virtual AtomicMode mode() const noexcept=0;
  virtual const AtomicDomainRef& domain() const noexcept=0;
  virtual std::span<const foundation::ObjectId> targets() const noexcept=0;
  virtual Result<void> validate() const=0;
  virtual Result<std::size_t> input_bytes() const=0;
  virtual std::optional<std::size_t> input_slot() const noexcept=0;
  virtual CppTypeToken input_type() const noexcept=0;
  virtual CppTypeToken result_type() const noexcept=0;
  virtual Result<CandidateValue> invoke(typename P::EditPort&,WorkContext&,std::span<const CandidateValue> previous={}) const=0;
};
}
