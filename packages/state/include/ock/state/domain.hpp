#pragma once
#include <ock/contracts/context.hpp>
#include <ock/contracts/outcome.hpp>
#include <ock/state/commit.hpp>
#include <ock/state/detail/published_state.hpp>
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

namespace ock::state {
// 组合根提供当前读授权；State 不依赖 Runtime 私有 Policy 对象。
class SnapshotAuthority : public contracts::PortLifetime {
public:
  virtual foundation::Result<void> authorize(const contracts::CallerView&,
      const contracts::AtomicDomainRef&) const=0;
};
struct DomainOptions {
  std::size_t root_bytes=0;
  std::size_t candidate_bytes=0;
  std::size_t result_bytes=0;
  std::size_t history_entries=0;
  std::size_t history_bytes=0;
  std::size_t snapshot_pins=0;
  std::size_t history_pins=0;
  std::size_t inflight_commits=0;
  std::size_t reclaim_batch=0;
};
namespace detail {
struct DomainIdentity {};
struct SnapshotLedger {
  explicit SnapshotLedger(std::size_t maximum):maximum(maximum) {}
  std::mutex mutex;
  const std::size_t maximum;
  std::size_t pins=0;
};
struct SnapshotCharge {
  explicit SnapshotCharge(std::shared_ptr<SnapshotLedger> value):ledger(std::move(value)) {}
  SnapshotCharge(const SnapshotCharge&)=delete;
  SnapshotCharge& operator=(const SnapshotCharge&)=delete;
  ~SnapshotCharge() {std::lock_guard lock(ledger->mutex);--ledger->pins;}
  std::shared_ptr<SnapshotLedger> ledger;
};
}
template<RootValue T> class StateDomain;
template<RootValue T> class HistoryView final {
public:
  HistoryView(const HistoryView&)=default;
  HistoryView(HistoryView&&) noexcept=default;
  HistoryView& operator=(HistoryView other) noexcept {charge_.swap(other.charge_);record_.swap(other.record_);return *this;}
  const HistoryRecord<T>& record() const noexcept {return *record_;}
private:
  friend class StateDomain<T>;
  HistoryView(std::shared_ptr<const HistoryRecord<T>> record,std::shared_ptr<detail::SnapshotCharge> charge)
      :charge_(std::move(charge)),record_(std::move(record)) {}
  std::shared_ptr<detail::SnapshotCharge> charge_;
  std::shared_ptr<const HistoryRecord<T>> record_;
};
template<RootValue T> class Snapshot final {
public:
  Snapshot(const Snapshot&)=default;
  Snapshot(Snapshot&&) noexcept=default;
  Snapshot& operator=(Snapshot other) noexcept {
    charge_.swap(other.charge_);identity_.swap(other.identity_);published_.swap(other.published_);return *this;
  }
  const T& value() const noexcept {return published_->root().value();}
  const contracts::AtomicDomainRef& domain() const noexcept {return published_->domain();}
  std::uint64_t revision() const noexcept {return published_->revision();}
  std::uint64_t history_cursor() const noexcept {return published_->history_cursor();}
  std::uint64_t lifecycle_generation() const noexcept {return published_->lifecycle_generation();}
private:
  friend class StateDomain<T>;
  Snapshot(std::shared_ptr<const detail::PublishedState<T>> published,
      std::shared_ptr<const detail::DomainIdentity> identity,std::shared_ptr<detail::SnapshotCharge> charge)
      :charge_(std::move(charge)),identity_(std::move(identity)),published_(std::move(published)) {}
  std::shared_ptr<detail::SnapshotCharge> charge_;
  std::shared_ptr<const detail::DomainIdentity> identity_;
  std::shared_ptr<const detail::PublishedState<T>> published_;
};
template<RootValue T> class StateDomain final : public contracts::PublicationAuthorityPort,
    public std::enable_shared_from_this<StateDomain<T>> {
  class Proof final:public contracts::PublicationProof {
  public:
    Proof(std::shared_ptr<const detail::DomainIdentity> identity,contracts::PublishedCommit value)
      :identity(std::move(identity)),value(std::move(value)) {}
    std::shared_ptr<const detail::DomainIdentity> identity;
    contracts::PublishedCommit value;
    std::atomic<bool> active{false};
  };
public:
  const contracts::AtomicDomainRef& domain() const noexcept {return domain_;}
  static foundation::Result<std::shared_ptr<StateDomain>> create(contracts::AtomicDomainRef domain,
      const T& initial,DomainOptions options,std::shared_ptr<const SnapshotAuthority> authority) {
    if(!contracts::valid_domain(domain)||!authority||!valid_options(options))
      return foundation::make_unexpected(error(StateErrc::InvalidRoot));
    auto root=FrozenRoot<T>::freeze(initial,options.root_bytes);
    if(!root)return foundation::make_unexpected(root.error());
    auto initial_state=detail::PublishedState<T>::create(domain,0,0,1,std::move(*root));
    if(!initial_state)return foundation::make_unexpected(initial_state.error());
    try {return std::shared_ptr<StateDomain>(new StateDomain(std::move(domain),options,std::move(authority),std::move(*initial_state)));}
    catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<Snapshot<T>> snapshot(const contracts::CallerView& caller) const {
    try {
      auto keep_alive=this->shared_from_this();
      // 外部 authority 可重入或抛异常；不在域锁内调用它。
      auto authorized=authority_->authorize(caller,domain_);
      if(!authorized)return foundation::make_unexpected(authorized.error());
      std::shared_ptr<const detail::PublishedState<T>> published;
      {std::lock_guard lock(mutex_);if(!current_)return foundation::make_unexpected(error(StateErrc::Closed));published=current_;}
      std::shared_ptr<detail::SnapshotCharge> charge;
      {
        std::lock_guard lock(pins_->mutex);
        if(pins_->pins==pins_->maximum)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
        charge=std::make_shared<detail::SnapshotCharge>(pins_);++pins_->pins;
      }
      return Snapshot<T>{std::move(published),identity_,std::move(charge)};
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
    catch(...) {return foundation::make_unexpected(error(StateErrc::InvalidCandidate));}
  }
  foundation::Result<void> validate_base(const Snapshot<T>& base) const {
    std::lock_guard lock(mutex_);
    if(!current_)return foundation::make_unexpected(error(StateErrc::Closed));
    if(base.identity_!=identity_||base.domain()!=domain_||base.lifecycle_generation()!=current_->lifecycle_generation())
      return foundation::make_unexpected(error(StateErrc::StaleGeneration));
    if(base.revision()!=current_->revision())return foundation::make_unexpected(error(StateErrc::RevisionConflict));
    return {};
  }
  foundation::Result<std::shared_ptr<const PreparedState<T>>> prepare(const Snapshot<T>& base,
      const T& candidate,const contracts::PreparedIdentity& prepared,std::size_t delta_bytes,
      std::span<const std::byte> receipt={},HistoryKind kind=HistoryKind::Edit,bool reversible=true) {
    if(!contracts::valid_prepared(prepared)||prepared.domain!=domain_||prepared.base_revision!=base.revision()||
        prepared.lifecycle_generation!=base.lifecycle_generation()||delta_bytes>options_.candidate_bytes)
      return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    if(base.revision()==std::numeric_limits<std::uint64_t>::max()||
        base.history_cursor()==std::numeric_limits<std::uint64_t>::max())
      return foundation::make_unexpected(error(StateErrc::Exhausted));
    auto frozen=FrozenRoot<T>::freeze(candidate,options_.root_bytes);
    if(!frozen)return foundation::make_unexpected(frozen.error());
    auto public_contract=contracts::PreparedCommit::create(prepared,receipt,options_.result_bytes);
    if(!public_contract)return foundation::make_unexpected(public_contract.error());
    try {
      auto next_revision=base.revision()+1,next_cursor=base.history_cursor()+1;
      auto publication=detail::PublishedState<T>::create(domain_,next_revision,next_cursor,
          base.lifecycle_generation(),*frozen);
      if(!publication)return foundation::make_unexpected(publication.error());
      auto history=std::shared_ptr<const HistoryRecord<T>>(new HistoryRecord<T>{prepared.commit,
          next_revision,next_cursor,kind,reversible,delta_bytes,base.published_->root(),*frozen});
      auto owned=std::shared_ptr<const PreparedState<T>>(new PreparedState<T>{prepared,
          std::move(*public_contract),std::move(*publication),std::move(history)});
      std::lock_guard lock(mutex_);
      auto valid=validate_base_locked(base);if(!valid)return foundation::make_unexpected(valid.error());
      if(active_)return foundation::make_unexpected(error(StateErrc::Busy));
      auto slot=(next_cursor-1)%history_.size();
      auto retained=history_bytes_-(history_[slot]?history_[slot]->delta_bytes()+sizeof(HistoryRecord<T>):0);
      auto added=delta_bytes+sizeof(HistoryRecord<T>);
      if(retained>options_.history_bytes||added>options_.history_bytes-retained)
        return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
      active_=owned;return owned;
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<std::shared_ptr<const PreparedState<T>>> prepare_undo(const Snapshot<T>& base,
      const contracts::PreparedIdentity& prepared,std::span<const std::byte> receipt={}) {
    std::shared_ptr<const HistoryRecord<T>> latest;
    {
      std::lock_guard lock(mutex_);auto valid=validate_base_locked(base);
      if(!valid)return foundation::make_unexpected(valid.error());
      if(!base.history_cursor())return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
      latest=history_[(base.history_cursor()-1)%history_.size()];
      if(!latest||latest->cursor()!=base.history_cursor()||!latest->reversible()||latest->kind()==HistoryKind::Undo)
        return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
    }
    return prepare(base,latest->before(),prepared,latest->delta_bytes(),receipt,HistoryKind::Undo,true);
  }
  foundation::Result<std::shared_ptr<const PreparedState<T>>> prepare_redo(const Snapshot<T>& base,
      const contracts::PreparedIdentity& prepared,std::span<const std::byte> receipt={}) {
    std::shared_ptr<const HistoryRecord<T>> latest;
    {
      std::lock_guard lock(mutex_);auto valid=validate_base_locked(base);
      if(!valid)return foundation::make_unexpected(valid.error());
      if(!base.history_cursor())return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
      latest=history_[(base.history_cursor()-1)%history_.size()];
      if(!latest||latest->cursor()!=base.history_cursor()||latest->kind()!=HistoryKind::Undo||!latest->reversible())
        return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
    }
    return prepare(base,latest->before(),prepared,latest->delta_bytes(),receipt,HistoryKind::Redo,true);
  }
  foundation::Result<HistoryView<T>> history(const contracts::CallerView& caller,std::uint64_t cursor) const {
    try {
      auto keep_alive=this->shared_from_this();
      auto authorized=authority_->authorize(caller,domain_);
      if(!authorized)return foundation::make_unexpected(authorized.error());
      std::shared_ptr<const HistoryRecord<T>> record;
      {std::lock_guard lock(mutex_);
        if(!current_)return foundation::make_unexpected(error(StateErrc::Closed));
        if(!cursor||history_.empty())return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));
        record=history_[(cursor-1)%history_.size()];
        if(!record||record->cursor()!=cursor)return foundation::make_unexpected(error(StateErrc::HistoryUnavailable));}
      std::shared_ptr<detail::SnapshotCharge> charge;
      {std::lock_guard lock(history_pins_->mutex);
        if(history_pins_->pins==history_pins_->maximum)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
        charge=std::make_shared<detail::SnapshotCharge>(history_pins_);++history_pins_->pins;}
      return HistoryView<T>{std::move(record),std::move(charge)};
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
    catch(...) {return foundation::make_unexpected(error(StateErrc::InvalidCandidate));}
  }
  foundation::Result<MemoryCommit> commit(std::shared_ptr<const PreparedState<T>> prepared,
      std::shared_ptr<const contracts::ActionPermit> permit,contracts::PermitAuthorityPort& authority,
      const contracts::PermitBinding& expected) noexcept {
    if(!prepared||!permit||expected.target!=domain_.domain_id||
        expected.lifecycle_generation!=prepared->identity_.lifecycle_generation)
      return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    std::shared_ptr<Proof> proof;
    try {
      contracts::PublishedCommit fact{prepared->identity_.commit,domain_,prepared->publication_->revision(),
          prepared->publication_->revision()};
      proof=std::make_shared<Proof>(identity_,fact); // claim 前预留证明存储。
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
    {
      std::lock_guard lock(mutex_);
      if(active_.get()!=prepared.get()||phase_!=Phase::Ready||close_requested_)
        return foundation::make_unexpected(error(close_requested_?StateErrc::Closed:StateErrc::InvalidCandidate));
      if(!current_||current_->revision()!=prepared->identity_.base_revision||
          current_->lifecycle_generation()!=prepared->identity_.lifecycle_generation)
        return foundation::make_unexpected(error(StateErrc::RevisionConflict));
      phase_=Phase::Claiming;
    }
    // Runtime authority 可重入 close/cancel；域锁外消费，Claiming 期间 close 只进入 draining。
    try {
      auto consumed=authority.consume(*permit,expected);
      if(!consumed) {finish_failed(prepared);return foundation::make_unexpected(consumed.error());}
    } catch(const std::bad_alloc&) {
      finish_failed(prepared);return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    } catch(...) {
      finish_failed(prepared);return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
    }
    std::shared_ptr<const HistoryRecord<T>> evicted;
    std::shared_ptr<const detail::PublishedState<T>> retired;
    {
      std::lock_guard lock(mutex_);
      foundation::invariant(active_.get()==prepared.get()&&phase_==Phase::Claiming);
      auto slot=(prepared->publication_->history_cursor()-1)%history_.size();
      evicted=std::move(history_[slot]);
      if(evicted)history_bytes_-=evicted->delta_bytes()+sizeof(HistoryRecord<T>);
      history_[slot]=prepared->history_;
      history_bytes_+=prepared->history_->delta_bytes()+sizeof(HistoryRecord<T>);
      current_=prepared->publication_;active_.reset();phase_=Phase::Ready;
      proof->active.store(true,std::memory_order_release);
      if(close_requested_)retired=std::move(current_);
    }
    evicted.reset();retired.reset(); // 旧根及用户正文一律锁外释放。
    auto copy_fact=[]<class To,class From>(const From& from) {To to;to.bytes=from.bytes;return to;};
    contracts::CommitFact committed{copy_fact.template operator()<contracts::FactId>(prepared->identity_.commit),
        prepared->identity_.commit,domain_,prepared->publication_->revision(),contracts::CommitDurability::Memory};
    auto published_id=copy_fact.template operator()<contracts::FactId>(prepared->identity_.commit);
    published_id.bytes[0]^=0x80;
    contracts::PublishedFact published{published_id,prepared->identity_.commit,prepared->publication_->revision()};
    return MemoryCommit{committed,published,proof->value,std::shared_ptr<const contracts::PublicationProof>(proof)};
  }
  foundation::Result<std::shared_ptr<const contracts::PublicationProof>>
  attest(const contracts::PublishedCommit& value) override {
    std::lock_guard lock(mutex_);
    bool found=false;
    for(const auto& row:history_)found=found||(row&&row->commit()==value.commit&&row->revision()==value.revision);
    if(!found||value.domain!=domain_||value.published_version!=value.revision)
      return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidProof));
    try {auto proof=std::make_shared<Proof>(identity_,value);proof->active.store(true);return std::shared_ptr<const contracts::PublicationProof>(proof);}
    catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<void> validate(const contracts::PublicationProof& candidate,
      const contracts::PublishedCommit& expected) const override {
    auto proof=dynamic_cast<const Proof*>(&candidate);
    if(!proof||proof->identity!=identity_||proof->value!=expected||!proof->active.load(std::memory_order_acquire))
      return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidProof));
    return {};
  }
  void close() {
    std::shared_ptr<const detail::PublishedState<T>> retired;
    std::shared_ptr<const PreparedState<T>> abandoned;
    {std::lock_guard lock(mutex_);close_requested_=true;
      if(phase_==Phase::Claiming)return;
      abandoned=std::move(active_);retired=std::move(current_);}
    // 最后一个根的析构可重入域；必须在锁外。
    abandoned.reset();retired.reset();
  }
  foundation::Result<void> reopen(const T& initial,std::uint64_t lifecycle_generation) {
    if(!lifecycle_generation)return foundation::make_unexpected(error(StateErrc::StaleGeneration));
    auto root=FrozenRoot<T>::freeze(initial,options_.root_bytes);
    if(!root)return foundation::make_unexpected(root.error());
    auto published=detail::PublishedState<T>::create(domain_,0,0,lifecycle_generation,std::move(*root));
    if(!published)return foundation::make_unexpected(published.error());
    try {
      auto next_identity=std::make_shared<const detail::DomainIdentity>();
      std::vector<std::shared_ptr<const HistoryRecord<T>>> empty(options_.history_entries);
      std::vector<std::shared_ptr<const HistoryRecord<T>>> retired;
      {
        std::lock_guard lock(mutex_);
        if(current_||active_||phase_!=Phase::Ready)return foundation::make_unexpected(error(StateErrc::Busy));
        if(lifecycle_generation<=lifecycle_generation_)
          return foundation::make_unexpected(error(StateErrc::StaleGeneration));
        retired.swap(history_);history_.swap(empty);history_bytes_=0;
        identity_=std::move(next_identity);lifecycle_generation_=lifecycle_generation;
        close_requested_=false;current_=std::move(*published);
      }
      // lifecycle 清理不占域锁；每个释放批次受显式 reclaim 上限约束。
      while(!retired.empty()) {
        auto count=std::min(options_.reclaim_batch,retired.size());
        while(count--)retired.pop_back();
      }
      return {};
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  std::size_t snapshot_pins() const {std::lock_guard lock(pins_->mutex);return pins_->pins;}
private:
  enum class Phase {Ready,Claiming};
  static bool valid_options(const DomainOptions& o) noexcept {
    return o.root_bytes&&o.candidate_bytes&&o.result_bytes&&o.history_entries&&o.history_bytes&&
      o.snapshot_pins&&o.history_pins&&o.inflight_commits==1&&o.reclaim_batch&&
      o.reclaim_batch<=o.history_entries;
  }
  foundation::Result<void> validate_base_locked(const Snapshot<T>& base) const {
    if(!current_)return foundation::make_unexpected(error(StateErrc::Closed));
    if(base.identity_!=identity_||base.domain()!=domain_||base.lifecycle_generation()!=current_->lifecycle_generation())
      return foundation::make_unexpected(error(StateErrc::StaleGeneration));
    if(base.revision()!=current_->revision())return foundation::make_unexpected(error(StateErrc::RevisionConflict));
    return {};
  }
  void finish_failed(const std::shared_ptr<const PreparedState<T>>& prepared) {
    std::shared_ptr<const detail::PublishedState<T>> retired;
    std::shared_ptr<const PreparedState<T>> abandoned;
    {std::lock_guard lock(mutex_);if(active_.get()==prepared.get()){abandoned=std::move(active_);phase_=Phase::Ready;}
      if(close_requested_)retired=std::move(current_);}
    abandoned.reset();retired.reset();
  }
  StateDomain(contracts::AtomicDomainRef domain,DomainOptions options,
      std::shared_ptr<const SnapshotAuthority> authority,std::shared_ptr<const detail::PublishedState<T>> initial)
      :domain_(std::move(domain)),options_(options),authority_(std::move(authority)),
       identity_(std::make_shared<const detail::DomainIdentity>()),pins_(std::make_shared<detail::SnapshotLedger>(options.snapshot_pins)),
       history_pins_(std::make_shared<detail::SnapshotLedger>(options.history_pins)),
       current_(std::move(initial)),history_(options.history_entries),lifecycle_generation_(1) {}
  const contracts::AtomicDomainRef domain_;
  const DomainOptions options_;
  std::shared_ptr<const SnapshotAuthority> authority_;
  std::shared_ptr<const detail::DomainIdentity> identity_;
  std::shared_ptr<detail::SnapshotLedger> pins_;
  std::shared_ptr<detail::SnapshotLedger> history_pins_;
  mutable std::mutex mutex_;
  std::shared_ptr<const detail::PublishedState<T>> current_;
  std::vector<std::shared_ptr<const HistoryRecord<T>>> history_;
  std::size_t history_bytes_=0;
  std::shared_ptr<const PreparedState<T>> active_;
  Phase phase_=Phase::Ready;
  bool close_requested_=false;
  std::uint64_t lifecycle_generation_;
};
}
