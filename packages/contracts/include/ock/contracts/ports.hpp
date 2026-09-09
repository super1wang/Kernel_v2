#pragma once
// D1.02：标准RAII领域work与窄材料协议，不实现调度或存储后端。
#include <atomic>
#include <ock/contracts/context.hpp>
#include <ock/contracts/outcome.hpp>
namespace ock::contracts {
// Async Read 的拥有型入口。保存 shared_ptr 即保留输入、上下文、Reader 和
// callback 访问寿命；complete 只提交候选，最后一个 owner 释放才完成收尾。
// complete 可并发仲裁；其余可变上下文访问须由业务串行使用。
template<AsyncInput A,ContractResult R,class Reader>
  requires (std::same_as<R,void> || AsyncInput<R>)
class AsyncReadCall : public PortLifetime {
public:
  virtual const A& input() const noexcept=0;
  virtual WorkContext& work() noexcept=0;
  virtual const Reader& reader() const noexcept=0;
  virtual Result<void> complete(Result<R>) noexcept=0;
};
class CompletionPort : public PortLifetime {
public:
  virtual Result<void> candidate_ready(Result<void>) noexcept = 0;
};
class ReadyWork : public PortLifetime {
public:
  virtual void execute() noexcept = 0;
};
class ExecutorPort : public PortLifetime {
public:
  virtual Result<void> submit(std::unique_ptr<ReadyWork>) = 0;
};
class TerminalReceiver : public PortLifetime {
public:
  virtual void terminal(ExecutionRef) noexcept = 0;
};
class CompletionLatch final {
public:
  explicit CompletionLatch(std::shared_ptr<CompletionPort> receiver)
      : receiver_(std::move(receiver)) {
    foundation::invariant(bool(receiver_));
  }
  CompletionLatch(const CompletionLatch &) = delete;
  CompletionLatch &operator=(const CompletionLatch &) = delete;
  Result<void> complete(Result<void> status) noexcept {
    bool expected = false;
    if (!claimed_.compare_exchange_strong(expected, true))
      return reject(ContractsErrc::DuplicateCompletion);
    return receiver_->candidate_ready(std::move(status));
  }

private:
  std::shared_ptr<CompletionPort> receiver_;
  std::atomic<bool> claimed_{false};
};
struct PreparedIdentity {
  CommitId commit;
  ReservationId reservation;
  AtomicDomainRef domain;
  std::uint64_t base_revision, lifecycle_generation;
  bool operator==(const PreparedIdentity &) const = default;
};
inline bool valid_prepared(const PreparedIdentity &p) {
  return !p.commit.empty() && !p.reservation.empty() &&
         valid_domain(p.domain) && p.lifecycle_generation != 0;
}
class PreparedCommit final {
public:
  PreparedCommit(const PreparedCommit &) = delete;
  PreparedCommit &operator=(const PreparedCommit &) = delete;
  PreparedCommit &operator=(PreparedCommit &&) = delete;
  static Result<std::shared_ptr<const PreparedCommit>>
  create(const PreparedIdentity &identity, std::span<const std::byte> receipt,
         std::size_t max_bytes) {
    if (!valid_prepared(identity))
      return make_unexpected(error(ContractsErrc::InvalidContract));
    if (receipt.size() > max_bytes)
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    return std::shared_ptr<const PreparedCommit>(
        new PreparedCommit(identity, receipt));
  }
  const PreparedIdentity &identity() const noexcept { return identity_; }
  std::span<const std::byte> receipt() const noexcept { return receipt_; }

private:
  PreparedCommit(const PreparedIdentity &i, std::span<const std::byte> b)
      : identity_(i), receipt_(b.begin(), b.end()) {}
  PreparedIdentity identity_;
  std::vector<std::byte> receipt_;
};
inline Result<void> validate_prepared_match(const PreparedIdentity &a,
                                            const PreparedIdentity &b) {
  if (!valid_prepared(a) || !valid_prepared(b) || a != b)
    return reject(ContractsErrc::InvalidContract);
  return {};
}
enum class CommitDisposition {
  Pending,
  KnownNotCommitted,
  DurableCommitted,
  Published,
  Indeterminate
};
struct CommitReport {
  PreparedIdentity identity;
  CommitDisposition disposition;
  std::optional<Error> error;
};
struct CommitState {
  PreparedIdentity identity;
  CommitDisposition disposition;
};
inline Result<void> validate_commit_report(const CommitState &current,
                                           const CommitReport &report) {
  auto same = validate_prepared_match(current.identity, report.identity);
  if (!same)
    return same;
  if (report.disposition == CommitDisposition::Pending ||
      report.disposition > CommitDisposition::Indeterminate)
    return reject(ContractsErrc::InvalidFact);
  if (current.disposition == CommitDisposition::Pending)
    return {};
  if (current.disposition == CommitDisposition::DurableCommitted &&
      (report.disposition == CommitDisposition::Published ||
       report.disposition == CommitDisposition::Indeterminate))
    return {};
  return reject(ContractsErrc::InvalidFact);
}
class CommitReceiver : public PortLifetime {
public:
  virtual void completed(CommitReport) noexcept = 0;
};
template <class P> class AtomicProviderPort : public PortLifetime {
public:
  virtual Result<std::unique_ptr<typename P::Frame>>
  begin(const AtomicDomainRef &) = 0;
  virtual Result<std::shared_ptr<const PreparedCommit>>
  prepare(typename P::Frame &, const PreparedIdentity &) = 0;
  virtual Result<void> commit(std::shared_ptr<const PreparedCommit>,
                              std::shared_ptr<const ActionPermit>,
                              std::shared_ptr<CommitReceiver>) = 0;
};
struct RecordRequest {
  ExecutionRef execution;
  std::optional<CommitId> commit;
  std::optional<EffectId> effect;
  Name kind;
  std::vector<std::byte> receipt;
  std::uint64_t capacity;
};
class RecordRequestSnapshot final {
public:
  RecordRequestSnapshot(const RecordRequestSnapshot &) = delete;
  RecordRequestSnapshot &operator=(const RecordRequestSnapshot &) = delete;
  static Result<std::shared_ptr<const RecordRequestSnapshot>>
  create(const RecordRequest &r, std::size_t max_bytes) {
    if (r.execution.execution_id.empty() || r.capacity == 0 ||
        (r.commit && r.commit->empty()) || (r.effect && r.effect->empty()))
      return make_unexpected(error(ContractsErrc::InvalidContract));
    if (r.receipt.size() > max_bytes)
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    return std::shared_ptr<const RecordRequestSnapshot>(
        new RecordRequestSnapshot(r));
  }
  const RecordRequest &value() const noexcept { return request_; }

private:
  explicit RecordRequestSnapshot(const RecordRequest &r) : request_(r) {}
  RecordRequest request_;
};
class RecordReservation : public PortLifetime {};
struct RecordReport {
  ExecutionRef execution;
  std::optional<CommitId> commit;
  std::optional<EffectId> effect;
  RequiredRecordState state;
  std::optional<RecordFailure> failure;
};
class RecordReceiver : public PortLifetime {
public:
  virtual void completed(RecordReport) noexcept = 0;
};
class RequiredRecordPort : public PortLifetime {
public:
  virtual Result<std::unique_ptr<RecordReservation>>
  reserve(const RecordRequest &) = 0;
  virtual Result<void> record(std::unique_ptr<RecordReservation>,
                              std::shared_ptr<const RecordRequestSnapshot>,
                              std::shared_ptr<RecordReceiver>) = 0;
};
inline Result<void> validate_record_report(const RecordRequest &request,
                                           RequiredRecordState previous,
                                           const RecordReport &report) {
  if (previous != RequiredRecordState::Pending ||
      report.execution != request.execution ||
      report.commit != request.commit || report.effect != request.effect ||
      (report.state != RequiredRecordState::Recorded &&
       report.state != RequiredRecordState::Failed) ||
      (report.state == RequiredRecordState::Failed) !=
          report.failure.has_value())
    return reject(ContractsErrc::InvalidFact);
  if (report.failure && (!report.failure->writes_blocked ||
                         !report.failure->reason.code().value() ||
                         report.failure->repair > RepairKind::ManualReview))
    return reject(ContractsErrc::InvalidFact);
  return {};
}
} // namespace ock::contracts
