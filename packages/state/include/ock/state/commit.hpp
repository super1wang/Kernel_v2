#pragma once
#include <ock/contracts/ports.hpp>
#include <ock/state/detail/published_state.hpp>
#include <ock/state/history.hpp>

namespace ock::state {
template<RootValue T> class StateDomain;

template<RootValue T> class PreparedState final {
public:
  const contracts::PreparedIdentity& identity() const noexcept {return identity_;}
  const std::shared_ptr<const contracts::PreparedCommit>& contract() const noexcept {return contract_;}
  std::size_t delta_bytes() const noexcept {return history_->delta_bytes();}
private:
  friend class StateDomain<T>;
  PreparedState(contracts::PreparedIdentity identity,
      std::shared_ptr<const contracts::PreparedCommit> contract,
      std::shared_ptr<const detail::PublishedState<T>> publication,
      std::shared_ptr<const HistoryRecord<T>> history)
      :identity_(std::move(identity)),contract_(std::move(contract)),publication_(std::move(publication)),
       history_(std::move(history)) {}
  contracts::PreparedIdentity identity_;
  std::shared_ptr<const contracts::PreparedCommit> contract_;
  std::shared_ptr<const detail::PublishedState<T>> publication_;
  std::shared_ptr<const HistoryRecord<T>> history_;
};

struct MemoryCommit final {
  contracts::CommitFact commit;
  contracts::PublishedFact published;
  contracts::PublishedCommit publication;
  std::shared_ptr<const contracts::PublicationProof> proof;
};
}
