#pragma once
#include <ock/state/atomic.hpp>

namespace ock::state {
// CoreContracts AtomicProviderPort 的内存实现。公开 PreparedCommit 只作 DTO key；
// 真正候选始终由本 provider 的 owned_ 保存并按指针与完整 identity 双重核对。
class ObjectMemoryProvider final:public contracts::AtomicProviderPort<ObjectStateProvider>,
    public std::enable_shared_from_this<ObjectMemoryProvider> {
public:
  static foundation::Result<std::shared_ptr<ObjectMemoryProvider>> create(
      std::shared_ptr<StateDomain<ObjectRoot>> domain,contracts::CallerView caller,EditOptions edit,
      std::shared_ptr<contracts::PermitAuthorityPort> permits,contracts::PermitBinding binding) {
    if(!domain||!permits||!edit.root_bytes||!edit.delta_bytes||!edit.changed_objects||
        binding.target.empty()||binding.target!=domain->domain().domain_id)
      return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    try {return std::shared_ptr<ObjectMemoryProvider>(new ObjectMemoryProvider(
        std::move(domain),std::move(caller),edit,std::move(permits),std::move(binding)));}
    catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<std::unique_ptr<ObjectStateProvider::Frame>>
  begin(const contracts::AtomicDomainRef& domain) override {
    if(domain!=domain_ref_)return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    auto snapshot=domain_->snapshot(caller_);if(!snapshot)return foundation::make_unexpected(snapshot.error());
    auto edit=ObjectEdit::begin(snapshot->value(),edit_);if(!edit)return foundation::make_unexpected(edit.error());
    try {return std::make_unique<ObjectStateProvider::Frame>(std::move(*snapshot),std::move(*edit));}
    catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<std::shared_ptr<const contracts::PreparedCommit>>
  prepare(ObjectStateProvider::Frame& frame,const contracts::PreparedIdentity& identity) override {
    auto candidate=frame.edit.freeze();if(!candidate)return foundation::make_unexpected(candidate.error());
    auto prepared=domain_->prepare(frame.base,candidate->value(),identity,frame.edit.delta_bytes());
    if(!prepared)return foundation::make_unexpected(prepared.error());
    std::lock_guard lock(mutex_);
    if(owned_)return foundation::make_unexpected(error(StateErrc::Busy));
    owned_=*prepared;return (*prepared)->contract();
  }
  foundation::Result<void> commit(std::shared_ptr<const contracts::PreparedCommit> prepared,
      std::shared_ptr<const contracts::ActionPermit> permit,std::shared_ptr<contracts::CommitReceiver> receiver) override {
    auto keep_alive=shared_from_this();
    if(!prepared||!permit||!receiver)return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    std::shared_ptr<const PreparedState<ObjectRoot>> owned;
    {
      std::lock_guard lock(mutex_);
      if(committing_||!owned_||owned_->contract().get()!=prepared.get()||
          contracts::validate_prepared_match(owned_->identity(),prepared->identity()).has_value()==false)
        return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidContract));
      committing_=true;owned=owned_;
    }
    auto result=domain_->commit(owned,std::move(permit),*permits_,binding_);
    contracts::CommitReport report{owned->identity(),result?contracts::CommitDisposition::Published:
        contracts::CommitDisposition::KnownNotCommitted,result?std::optional<foundation::Error>{}:
        std::optional<foundation::Error>{result.error()}};
    {
      std::lock_guard lock(mutex_);owned_.reset();committing_=false;if(result)published_=*result;
    }
    receiver->completed(std::move(report)); // owner 和域锁之外，允许 inline 重入。
    return {};
  }
  foundation::Result<MemoryCommit> published(const contracts::PreparedIdentity& identity) const {
    std::lock_guard lock(mutex_);
    if(!published_||published_->publication.commit!=identity.commit||published_->publication.domain!=identity.domain)
      return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
    return *published_;
  }
private:
  ObjectMemoryProvider(std::shared_ptr<StateDomain<ObjectRoot>> domain,contracts::CallerView caller,EditOptions edit,
      std::shared_ptr<contracts::PermitAuthorityPort> permits,contracts::PermitBinding binding)
      :domain_(std::move(domain)),caller_(std::move(caller)),edit_(edit),permits_(std::move(permits)),binding_(std::move(binding)),
       domain_ref_(domain_->domain()) {}
  std::shared_ptr<StateDomain<ObjectRoot>> domain_;
  contracts::CallerView caller_;
  EditOptions edit_;
  std::shared_ptr<contracts::PermitAuthorityPort> permits_;
  contracts::PermitBinding binding_;
  contracts::AtomicDomainRef domain_ref_;
  mutable std::mutex mutex_;
  std::shared_ptr<const PreparedState<ObjectRoot>> owned_;
  std::optional<MemoryCommit> published_;
  bool committing_=false;
};
}
