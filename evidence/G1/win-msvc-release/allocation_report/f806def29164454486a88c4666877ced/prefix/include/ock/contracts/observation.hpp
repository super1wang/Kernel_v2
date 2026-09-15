#pragma once
// D1.02：类型化有界观察，完成可靠性不依赖有损提示。
#include <ock/contracts/context.hpp>
#include <ock/contracts/outcome.hpp>
namespace ock::contracts {
class ObservationVersion final {
public:
  static Result<ObservationVersion> create(std::uint64_t v) {
    if (!v)
      return make_unexpected(error(ContractsErrc::StaleObservation));
    return ObservationVersion(v);
  }
  std::uint64_t value() const noexcept { return value_; }
  Result<ObservationVersion> next() const noexcept {
    auto n = foundation::checked_add(value_, std::uint64_t{1});
    if (!n)
      return make_unexpected(n.error());
    return ObservationVersion(*n);
  }
  bool operator==(const ObservationVersion &) const = default;

private:
  explicit ObservationVersion(std::uint64_t v) : value_(v) {}
  std::uint64_t value_;
};
enum class FactKind {
  Commit,
  Published,
  Effect,
  Lifecycle,
  Unknown,
  Resolution
};
struct FactSummary {
  FactId fact;
  FactKind kind;
  Application application;
  // 业务引用保持强类型；旧的纯列表摘要可以缺省，通知不得用 FactId 替代。
  std::optional<std::variant<CommitId, EffectId, TransitionId, FactId>> reference;
  bool operator==(const FactSummary &) const = default;
};
inline FactSummary summarize_fact(const Fact &fact) {
  return std::visit([](const auto &f) -> FactSummary {
    using F = std::decay_t<decltype(f)>;
    if constexpr (std::same_as<F, CommitFact>)
      return {f.fact_id, FactKind::Commit, Application::Applied, f.commit};
    else if constexpr (std::same_as<F, PublishedFact>)
      return {f.fact_id, FactKind::Published, Application::Applied, f.commit};
    else if constexpr (std::same_as<F, EffectFact>)
      return {f.fact_id, FactKind::Effect, f.application, f.effect};
    else if constexpr (std::same_as<F, LifecycleFact>)
      return {f.fact_id, FactKind::Lifecycle, Application::Applied, f.transition};
    else if constexpr (std::same_as<F, UnknownFact>)
      return std::visit([&](const auto &ref) -> FactSummary {
        return {f.fact_id, FactKind::Unknown, Application::NotApplied, ref};
      }, f.reference);
    else
      return {f.fact_id, FactKind::Resolution,
              f.determination == Determination::Applied ? Application::Applied
                                                        : Application::NotApplied,
              f.unknown_id};
  }, fact);
}
struct ProgressSummary {
  std::uint64_t completed = 0, total = 0;
  ResultScope scope = ResultScope::ReadOnly;
  bool operator==(const ProgressSummary &) const = default;
};
struct SummaryInput {
  ExecutionRef execution;
  OperationKey operation;
  PrincipalRef owner;
  std::optional<ExecutionRef> parent;
  ExecutionPhase phase;
  HostIncarnation host;
  ObservationVersion version;
  ProgressSummary progress;
  std::vector<FactSummary> facts;
  EvidenceState evidence = EvidenceState::Volatile;
  RequiredRecordState record_state = RequiredRecordState::NotRequired;
  std::optional<Error> fault;
  bool writes_blocked = false;
  std::optional<RepairKind> repair;
};
inline Result<void> validate_summary(const SummaryInput &s) {
  if (s.execution.execution_id.empty() || s.owner.principal_id.empty() ||
      s.host.empty() || s.phase > ExecutionPhase::Terminal ||
      s.evidence > EvidenceState::PersistenceUncertain ||
      s.record_state > RequiredRecordState::Failed)
    return reject(ContractsErrc::InvalidFact);
  if (s.parent && (s.parent->execution_id.empty() || *s.parent == s.execution))
    return reject(ContractsErrc::InvalidFact);
  if (s.facts.size() > 8 || s.progress.completed > s.progress.total ||
      s.progress.scope > ResultScope::Candidate)
    return reject(ContractsErrc::BudgetExceeded);
  for (std::size_t i = 0; i < s.facts.size(); ++i) {
    const auto &f = s.facts[i];
    if (f.fact.empty() || f.kind > FactKind::Resolution ||
        f.application > Application::PartiallyApplied)
      return reject(ContractsErrc::InvalidFact);
    if (f.reference) {
      const auto index = f.reference->index();
      if (std::visit([](const auto &id) { return id.empty(); }, *f.reference) ||
          ((f.kind == FactKind::Commit || f.kind == FactKind::Published) && index != 0) ||
          (f.kind == FactKind::Effect && index != 1) ||
          (f.kind == FactKind::Lifecycle && index != 2) ||
          (f.kind == FactKind::Unknown && index > 1) ||
          (f.kind == FactKind::Resolution && index != 3))
        return reject(ContractsErrc::InvalidFact);
    }
    for (std::size_t j = 0; j < i; ++j)
      if (s.facts[j].fact == f.fact)
        return reject(ContractsErrc::InvalidFact);
  }
  if (s.fault && s.fault->info())
    return reject(ContractsErrc::BudgetExceeded);
  bool failed = s.record_state == RequiredRecordState::Failed;
  if (failed != (s.evidence == EvidenceState::RequiredRecordFailed) ||
      failed != s.repair.has_value() || failed != s.writes_blocked)
    return reject(ContractsErrc::InvalidFact);
  if (failed && (!s.fault || s.fault->code().value() == 0 ||
                 *s.repair > RepairKind::ManualReview))
    return reject(ContractsErrc::InvalidFact);
  if (s.phase == ExecutionPhase::Terminal &&
      s.record_state == RequiredRecordState::Pending)
    return reject(ContractsErrc::InvalidPhase);
  return {};
}
class ExecutionSummary final {
public:
  ExecutionSummary(const ExecutionSummary &) = delete;
  ExecutionSummary &operator=(const ExecutionSummary &) = delete;
  ExecutionSummary &operator=(ExecutionSummary &&) = delete;
  static Result<std::shared_ptr<const ExecutionSummary>>
  create(const SummaryInput &s) {
    auto valid = validate_summary(s);
    if (!valid)
      return make_unexpected(valid.error());
    return std::shared_ptr<const ExecutionSummary>(new ExecutionSummary(s));
  }
  const SummaryInput &value() const noexcept { return value_; }

private:
  explicit ExecutionSummary(const SummaryInput &s) : value_(s) {}
  SummaryInput value_;
};
struct ListBudget {
  std::uint32_t page_size = 50, scan_limit = 2000;
};
struct KeysetPosition {
  HostIncarnation host;
  std::uint64_t upper_ordinal, before_ordinal;
};
enum class PhaseSet { Nonterminal, Terminal, All };
struct ListRequest {
  PrincipalRef owner;
  PhaseSet phases;
  ListBudget budget;
  std::optional<KeysetPosition> position;
};
struct ListedSummary {
  std::uint64_t listing_ordinal;
  std::shared_ptr<const ExecutionSummary> summary;
};
struct ListPage {
  HostIncarnation host;
  std::vector<ListedSummary> items;
  std::optional<KeysetPosition> next;
  Name retention_scope;
};
enum class ObservationTopic { Progress, Phase, Fact };
struct ObservationFilter {
  std::vector<ExecutionRef> executions;
  std::optional<PrincipalRef> owner;
  std::vector<ObservationTopic> topics;
};
struct ChangeHint {
  std::shared_ptr<const ExecutionSummary> summary;
  ObservationTopic topic;
  bool gap;
};
class ObservationReceiver : public PortLifetime {
public:
  virtual void changed(ChangeHint) noexcept = 0;
};
class ObservationLease : public PortLifetime {};
class ObservationPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<const ExecutionSummary>>
  get_summary(const CallerView &, ExecutionRef) = 0;
  virtual Result<ListPage> list_summaries(const CallerView &,
                                          const ListRequest &) = 0;
  virtual Result<std::unique_ptr<ObservationLease>>
  observe_changes(const CallerView &, const ObservationFilter &,
                  std::shared_ptr<ObservationReceiver>) = 0;
};
inline Result<void>
validate_observation_filter(const ObservationFilter &f,
                            std::size_t max_executions = 200) {
  if (f.executions.empty() == !f.owner.has_value() ||
      f.executions.size() > max_executions || f.topics.empty() ||
      f.topics.size() > 3)
    return reject(ContractsErrc::InvalidContract);
  if (f.owner && f.owner->principal_id.empty())
    return reject(ContractsErrc::InvalidContract);
  for (std::size_t i = 0; i < f.executions.size(); ++i) {
    if (f.executions[i].execution_id.empty())
      return reject(ContractsErrc::InvalidContract);
    for (std::size_t j = 0; j < i; ++j)
      if (f.executions[i] == f.executions[j])
        return reject(ContractsErrc::InvalidContract);
  }
  for (std::size_t i = 0; i < f.topics.size(); ++i) {
    if (f.topics[i] > ObservationTopic::Fact)
      return reject(ContractsErrc::InvalidContract);
    for (std::size_t j = 0; j < i; ++j)
      if (f.topics[i] == f.topics[j])
        return reject(ContractsErrc::InvalidContract);
  }
  return {};
}
enum class UpdateKind { PartialHint, Complete };
enum class UpdateDecision { Accept, Ignore, Resync };
inline Result<UpdateDecision>
validate_summary_update(const SummaryInput &previous,
                        const SummaryInput &incoming, UpdateKind kind) {
  auto a = validate_summary(previous), b = validate_summary(incoming);
  if (!a)
    return make_unexpected(a.error());
  if (!b)
    return make_unexpected(b.error());
  if (previous.execution != incoming.execution)
    return make_unexpected(error(ContractsErrc::InvalidFact));
  if (previous.host != incoming.host)
    return UpdateDecision::Resync;
  if (incoming.version.value() < previous.version.value())
    return UpdateDecision::Ignore;
  if (previous.phase == ExecutionPhase::Terminal &&
      incoming.phase != ExecutionPhase::Terminal)
    return make_unexpected(error(ContractsErrc::InvalidPhase));
  if (incoming.version == previous.version) {
    if (kind == UpdateKind::PartialHint)
      return UpdateDecision::Ignore;
    if (previous.phase != incoming.phase ||
        previous.operation != incoming.operation ||
        previous.owner != incoming.owner || previous.parent != incoming.parent ||
        previous.progress != incoming.progress || previous.evidence != incoming.evidence ||
        previous.record_state != incoming.record_state ||
        previous.writes_blocked != incoming.writes_blocked || previous.repair != incoming.repair ||
        previous.fault.has_value() != incoming.fault.has_value() ||
        (previous.fault && previous.fault->code() != incoming.fault->code()))
      return make_unexpected(error(ContractsErrc::StaleObservation));
    for (const auto &f : previous.facts) {
      auto found = std::find(incoming.facts.begin(), incoming.facts.end(), f);
      if (found == incoming.facts.end())
        return make_unexpected(error(ContractsErrc::StaleObservation));
    }
  }
  return UpdateDecision::Accept;
}
inline Result<void> validate_list_page(const ListRequest &request,
                                       const ListPage &page,
                                       ListBudget configured) {
  if (page.host.empty() || request.owner.principal_id.empty() ||
      request.phases > PhaseSet::All || request.budget.page_size == 0 ||
      request.budget.page_size > 200 ||
      request.budget.page_size > configured.page_size ||
      request.budget.scan_limit == 0 ||
      request.budget.scan_limit > configured.scan_limit ||
      page.items.size() > request.budget.page_size ||
      page.items.size() > request.budget.scan_limit)
    return reject(ContractsErrc::BudgetExceeded);
  if (request.position &&
      (request.position->host != page.host ||
       request.position->before_ordinal == 0 ||
       request.position->before_ordinal > request.position->upper_ordinal))
    return reject(ContractsErrc::HostMismatch);
  std::uint64_t last = (std::numeric_limits<std::uint64_t>::max)();
  for (const auto &item : page.items) {
    if (!item.summary || item.listing_ordinal == 0 ||
        item.listing_ordinal >= last)
      return reject(ContractsErrc::InvalidFact);
    last = item.listing_ordinal;
    const auto &s = item.summary->value();
    if (s.host != page.host || s.owner != request.owner ||
        (request.phases == PhaseSet::Terminal &&
         s.phase != ExecutionPhase::Terminal) ||
        (request.phases == PhaseSet::Nonterminal &&
         s.phase == ExecutionPhase::Terminal))
      return reject(ContractsErrc::InvalidGrant);
    if (request.position &&
        (item.listing_ordinal >= request.position->before_ordinal ||
         item.listing_ordinal > request.position->upper_ordinal))
      return reject(ContractsErrc::InvalidFact);
  }
  if (page.next) {
    const auto &next = *page.next;
    if (next.host != page.host || next.before_ordinal == 0 ||
        next.before_ordinal > next.upper_ordinal)
      return reject(ContractsErrc::InvalidFact);
    if (request.position &&
        (next.upper_ordinal != request.position->upper_ordinal ||
         next.before_ordinal >= request.position->before_ordinal))
      return reject(ContractsErrc::InvalidFact);
    if (!page.items.empty() &&
        next.before_ordinal > page.items.back().listing_ordinal)
      return reject(ContractsErrc::InvalidFact);
  }
  return {};
}
} // namespace ock::contracts
