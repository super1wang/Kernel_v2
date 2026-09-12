#pragma once
#include <ock/contracts/context.hpp>
#include <ock/state/detail/published_state.hpp>
#include <mutex>

namespace ock::state {
// 组合根提供当前读授权；State 不依赖 Runtime 私有 Policy 对象。
class SnapshotAuthority : public contracts::PortLifetime {
public:
  virtual foundation::Result<void> authorize(const contracts::CallerView&,
      const contracts::AtomicDomainRef&) const=0;
};
struct DomainOptions {
  std::size_t root_bytes;
  std::size_t snapshot_pins;
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
template<RootValue T> class StateDomain final : public std::enable_shared_from_this<StateDomain<T>> {
public:
  static foundation::Result<std::shared_ptr<StateDomain>> create(contracts::AtomicDomainRef domain,
      const T& initial,DomainOptions options,std::shared_ptr<const SnapshotAuthority> authority) {
    if(!contracts::valid_domain(domain)||!authority||!options.root_bytes||!options.snapshot_pins)
      return foundation::make_unexpected(error(StateErrc::InvalidRoot));
    auto root=FrozenRoot<T>::freeze(initial,options.root_bytes);
    if(!root)return foundation::make_unexpected(root.error());
    auto initial_state=detail::PublishedState<T>::create(domain,0,0,1,std::move(*root));
    if(!initial_state)return foundation::make_unexpected(initial_state.error());
    try {return std::shared_ptr<StateDomain>(new StateDomain(std::move(domain),options,std::move(authority),std::move(*initial_state)));}
    catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
  }
  foundation::Result<Snapshot<T>> snapshot(const contracts::CallerView& caller) const {
    auto keep_alive=this->shared_from_this();
    // 外部 authority 可重入；不在域锁内调用它。
    auto authorized=authority_->authorize(caller,domain_);
    if(!authorized)return foundation::make_unexpected(authorized.error());
    try {
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
  }
  foundation::Result<void> validate_base(const Snapshot<T>& base) const {
    std::lock_guard lock(mutex_);
    if(!current_)return foundation::make_unexpected(error(StateErrc::Closed));
    if(base.identity_!=identity_||base.domain()!=domain_||base.lifecycle_generation()!=current_->lifecycle_generation())
      return foundation::make_unexpected(error(StateErrc::StaleGeneration));
    if(base.revision()!=current_->revision())return foundation::make_unexpected(error(StateErrc::RevisionConflict));
    return {};
  }
  void close() {
    std::shared_ptr<const detail::PublishedState<T>> retired;
    {std::lock_guard lock(mutex_);retired=std::move(current_);}
    // 最后一个根的析构可重入域；必须在锁外。
    retired.reset();
  }
  std::size_t snapshot_pins() const {std::lock_guard lock(pins_->mutex);return pins_->pins;}
private:
  StateDomain(contracts::AtomicDomainRef domain,DomainOptions options,
      std::shared_ptr<const SnapshotAuthority> authority,std::shared_ptr<const detail::PublishedState<T>> initial)
      :domain_(std::move(domain)),options_(options),authority_(std::move(authority)),
       identity_(std::make_shared<const detail::DomainIdentity>()),pins_(std::make_shared<detail::SnapshotLedger>(options.snapshot_pins)),current_(std::move(initial)) {}
  const contracts::AtomicDomainRef domain_;
  const DomainOptions options_;
  std::shared_ptr<const SnapshotAuthority> authority_;
  std::shared_ptr<const detail::DomainIdentity> identity_;
  std::shared_ptr<detail::SnapshotLedger> pins_;
  mutable std::mutex mutex_;
  std::shared_ptr<const detail::PublishedState<T>> current_;
};
}
