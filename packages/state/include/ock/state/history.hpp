#pragma once
#include <ock/state/root.hpp>
#include <mutex>

namespace ock::state {
namespace detail {
struct HistoryLedger {std::mutex mutex;std::size_t bytes=0;};
struct HistoryCharge {
  std::shared_ptr<HistoryLedger> ledger;std::size_t bytes;
  HistoryCharge(std::shared_ptr<HistoryLedger> value,std::size_t count):ledger(std::move(value)),bytes(count) {}
  ~HistoryCharge(){std::lock_guard lock(ledger->mutex);ledger->bytes-=bytes;}
};
}
enum class HistoryKind { Edit, Undo, Redo };

template<RootValue T> class HistoryRecord final {
public:
  const contracts::CommitId& commit() const noexcept {return commit_;}
  std::uint64_t revision() const noexcept {return revision_;}
  std::uint64_t cursor() const noexcept {return cursor_;}
  HistoryKind kind() const noexcept {return kind_;}
  bool reversible() const noexcept {return reversible_;}
  std::size_t delta_bytes() const noexcept {return delta_bytes_;}
  const T& before() const noexcept {return before_.value();}
  const T& after() const noexcept {return after_.value();}
private:
  template<RootValue> friend class StateDomain;
  HistoryRecord(contracts::CommitId commit,std::uint64_t revision,std::uint64_t cursor,
      HistoryKind kind,bool reversible,std::size_t delta,FrozenRoot<T> before,FrozenRoot<T> after,
      std::shared_ptr<detail::HistoryCharge> charge)
      :commit_(commit),revision_(revision),cursor_(cursor),kind_(kind),reversible_(reversible),
       charge_(std::move(charge)),delta_bytes_(delta),before_(std::move(before)),after_(std::move(after)) {}
  contracts::CommitId commit_;
  std::uint64_t revision_,cursor_;
  HistoryKind kind_;
  bool reversible_;
  std::shared_ptr<detail::HistoryCharge> charge_;
  std::size_t delta_bytes_;
  FrozenRoot<T> before_,after_;
};
}
