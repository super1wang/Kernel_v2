#pragma once
// D1.02：事实集合的内部验证不替代接收owner的来源与历史验证。
#include <algorithm>
#include <ock/contracts/identity.hpp>
namespace ock::contracts {
enum class EvidenceState {
  Volatile,
  Durable,
  RequiredRecordFailed,
  PersistenceUncertain
};
// 控制仲裁结果不表示业务已经停止，也不改写执行事实。
enum class CancelDisposition { Requested, AlreadyClaimed, AlreadyTerminal };
enum class ExecutionPhase {
  Queued,
  WaitingResources,
  Running,
  WaitingChild,
  Finalizing,
  Suspended,
  Terminal
};
enum class Application { NotApplied, Applied, PartiallyApplied };
enum class BusinessStatus { Succeeded, Failed };
enum class CommitDurability { Memory, DurableCommitted };
enum class UnknownBoundary { ExternalEffect, StorageCommit };
enum class Determination { Applied, NotApplied };
struct CommitFact {
  FactId fact_id;
  CommitId commit;
  AtomicDomainRef domain;
  std::uint64_t revision;
  CommitDurability durability;
  bool operator==(const CommitFact &) const = default;
};
struct PublishedFact {
  FactId fact_id;
  CommitId commit;
  std::uint64_t published_version;
  bool operator==(const PublishedFact &) const = default;
};
struct EffectFact {
  FactId fact_id;
  EffectId effect;
  Application application;
  bool operator==(const EffectFact &) const = default;
};
struct LifecycleFact {
  FactId fact_id;
  TransitionId transition;
  Name before, after;
  std::uint64_t generation;
  bool operator==(const LifecycleFact &) const = default;
};
struct UnknownFact {
  FactId fact_id;
  UnknownBoundary boundary;
  std::variant<CommitId, EffectId> reference;
  std::string reconcile;
  bool operator==(const UnknownFact &) const = default;
};
struct ResolutionRecord {
  FactId fact_id;
  FactId unknown_id;
  Determination determination;
  std::string evidence_ref;
  bool operator==(const ResolutionRecord &) const = default;
};
using Fact = std::variant<CommitFact, PublishedFact, EffectFact, LifecycleFact,
                          UnknownFact, ResolutionRecord>;
struct FactBudget {
  std::size_t max_facts, max_steps, max_text_bytes;
};
inline FactId fact_id(const Fact &f) {
  return std::visit([](const auto &v) { return v.fact_id; }, f);
}
namespace detail {
inline bool good_text(std::string_view s) {
  return !s.empty() && foundation::detail::valid_utf8(s);
}
inline Result<void> validate_facts(std::span<const Fact> fs,
                                   FactBudget budget) {
  if (fs.size() > budget.max_facts)
    return reject(ContractsErrc::BudgetExceeded);
  std::size_t bytes = 0;
  for (std::size_t i = 0; i < fs.size(); ++i) {
    if (fact_id(fs[i]).empty())
      return reject(ContractsErrc::InvalidFact);
    for (std::size_t j = 0; j < i; ++j)
      if (fact_id(fs[j]) == fact_id(fs[i]))
        return reject(ContractsErrc::InvalidFact);
    auto valid = std::visit(
        [&](const auto &f) -> Result<void> {
          using F = std::decay_t<decltype(f)>;
          if constexpr (std::same_as<F, CommitFact>) {
            if (f.commit.empty() || !valid_domain(f.domain) ||
                f.revision == 0 ||
                f.durability > CommitDurability::DurableCommitted)
              return reject(ContractsErrc::InvalidFact);
            for (std::size_t j = 0; j < i; ++j)
              if (auto old = std::get_if<CommitFact>(&fs[j]);
                  old && old->commit == f.commit)
                return reject(ContractsErrc::ContradictoryFact);
          } else if constexpr (std::same_as<F, PublishedFact>) {
            const CommitFact *commit = nullptr;
            for (std::size_t j = 0; j < i; ++j) {
              if (auto c = std::get_if<CommitFact>(&fs[j]);
                  c && c->commit == f.commit)
                commit = c;
              if (auto old = std::get_if<PublishedFact>(&fs[j]);
                  old && old->commit == f.commit)
                return reject(ContractsErrc::InvalidFact);
            }
            if (!commit || f.published_version != commit->revision)
              return reject(ContractsErrc::InvalidFact);
          } else if constexpr (std::same_as<F, EffectFact>) {
            if (f.effect.empty() ||
                f.application > Application::PartiallyApplied)
              return reject(ContractsErrc::InvalidFact);
            for (std::size_t j = 0; j < i; ++j)
              if (auto old = std::get_if<EffectFact>(&fs[j]);
                  old && old->effect == f.effect)
                return reject(ContractsErrc::ContradictoryFact);
          } else if constexpr (std::same_as<F, LifecycleFact>) {
            if (f.transition.empty() || !f.generation)
              return reject(ContractsErrc::InvalidFact);
            for (std::size_t j = 0; j < i; ++j)
              if (auto old = std::get_if<LifecycleFact>(&fs[j]);
                  old && old->transition == f.transition)
                return reject(ContractsErrc::ContradictoryFact);
          } else if constexpr (std::same_as<F, UnknownFact>) {
            if (f.boundary > UnknownBoundary::StorageCommit ||
                !good_text(f.reconcile) ||
                (f.boundary == UnknownBoundary::ExternalEffect) !=
                    std::holds_alternative<EffectId>(f.reference) ||
                std::visit([](const auto &id) { return id.empty(); },
                           f.reference))
              return reject(ContractsErrc::InvalidFact);
            if (f.reconcile.size() > budget.max_text_bytes - bytes)
              return reject(ContractsErrc::BudgetExceeded);
            bytes += f.reconcile.size();
            for (std::size_t j = 0; j < i; ++j)
              if (auto old = std::get_if<UnknownFact>(&fs[j]);
                  old && old->reference == f.reference)
                return reject(ContractsErrc::InvalidFact);
          } else {
            if (f.determination > Determination::NotApplied ||
                !good_text(f.evidence_ref))
              return reject(ContractsErrc::InvalidFact);
            if (f.evidence_ref.size() > budget.max_text_bytes - bytes)
              return reject(ContractsErrc::BudgetExceeded);
            bytes += f.evidence_ref.size();
            bool found = false;
            for (std::size_t j = 0; j < i; ++j) {
              if (auto u = std::get_if<UnknownFact>(&fs[j]);
                  u && u->fact_id == f.unknown_id)
                found = true;
              if (auto old = std::get_if<ResolutionRecord>(&fs[j]);
                  old && old->unknown_id == f.unknown_id)
                return reject(ContractsErrc::InvalidFact);
            }
            if (!found)
              return reject(ContractsErrc::InvalidFact);
          }
          return {};
        },
        fs[i]);
    if (!valid)
      return valid;
  }
  // 对整个集合重验历史对账，后追加的确定事实也不能推翻旧结论。
  for (const auto &f : fs)
    if (auto r = std::get_if<ResolutionRecord>(&f)) {
      const UnknownFact *u = nullptr;
      for (const auto &x : fs)
        if (auto v = std::get_if<UnknownFact>(&x);
            v && v->fact_id == r->unknown_id)
          u = v;
      bool has_determination = false;
      for (const auto &x : fs) {
        if (auto c = std::get_if<CommitFact>(&x);
            c && std::holds_alternative<CommitId>(u->reference) &&
            c->commit == std::get<CommitId>(u->reference)) {
          if (r->determination == Determination::NotApplied)
            return reject(ContractsErrc::ContradictoryFact);
          has_determination = true;
        }
        if (auto e = std::get_if<EffectFact>(&x);
            e && std::holds_alternative<EffectId>(u->reference) &&
            e->effect == std::get<EffectId>(u->reference)) {
          bool applied = e->application != Application::NotApplied;
          if (applied != (r->determination == Determination::Applied))
            return reject(ContractsErrc::ContradictoryFact);
          has_determination = true;
        }
      }
      if (!has_determination && r->determination == Determination::Applied)
        return reject(ContractsErrc::InvalidFact);
    }
  return {};
}
} // namespace detail
class KnownFacts final {
public:
  KnownFacts(const KnownFacts &) = delete;
  KnownFacts &operator=(const KnownFacts &) = delete;
  KnownFacts &operator=(KnownFacts &&) = delete;
  static Result<std::shared_ptr<const KnownFacts>>
  create(std::span<const Fact> fs, FactBudget budget) {
    auto v = detail::validate_facts(fs, budget);
    if (!v)
      return make_unexpected(v.error());
    return std::shared_ptr<const KnownFacts>(new KnownFacts(fs));
  }
  Result<std::shared_ptr<const KnownFacts>>
  appended(std::span<const Fact> suffix, FactBudget budget) const {
    if (suffix.size() > budget.max_facts ||
        values_.size() > budget.max_facts - suffix.size())
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    auto next = values_;
    next.insert(next.end(), suffix.begin(), suffix.end());
    return create(next, budget);
  }
  std::span<const Fact> values() const noexcept { return values_; }

private:
  explicit KnownFacts(std::span<const Fact> fs)
      : values_(fs.begin(), fs.end()) {}
  std::vector<Fact> values_;
};
inline Result<void> validate_facts_append(const KnownFacts &old,
                                          const KnownFacts &next) {
  if (old.values().size() > next.values().size() ||
      !std::equal(old.values().begin(), old.values().end(),
                  next.values().begin()))
    return reject(ContractsErrc::ContradictoryFact);
  return detail::validate_facts(
      next.values(),
      {next.values().size(), 0, (std::numeric_limits<std::size_t>::max)()});
}
template <class R> using ResultMaterial = Result<R>;
template <class R> struct EffectReport {
  EffectId effect;
  std::optional<Application> application;
  BusinessStatus status;
  std::vector<std::string> external_evidence;
  ResultMaterial<R> result;
  std::string reconcile;
};
template <class R> struct TransitionReport {
  TransitionId transition;
  foundation::ObjectId target;
  Name before, after;
  std::uint64_t generation;
  BusinessStatus status;
  ResultMaterial<R> result;
};
enum class ResultScope { ReadOnly, Candidate };
enum class StepStatus { Succeeded, Failed, Cancelled };
enum class ChildResult { Succeeded, Failed, Unknown };
struct StepSummary {
  Name step_id;
  StepStatus status;
  bool required;
  ChildResult actual;
  bool finalized;
};
template <class R> struct ReadCompleted {
  Result<R> result;
  ResultScope scope;
};
template <class R> struct StateCommitted {
  CommitId commit;
  AtomicDomainRef domain;
  std::uint64_t revision, published_version;
  Result<R> result;
};
template <class R> struct EffectResolved {
  EffectReport<R> report;
};
template <class R> struct LifecycleResolved {
  TransitionReport<R> report;
};
template <class R> struct PlanCompleted {
  Result<R> exports;
  std::vector<StepSummary> steps;
};
struct FailedBeforeApply {
  Name failure_phase;
  Error reason;
};
struct CancelledBeforeApply {
  Error reason;
};
struct PartialCompletion {
  std::vector<StepSummary> steps;
};
struct Indeterminate {
  std::vector<FactId> unknown_ids;
};
struct PublishedCommit {
  CommitId commit;
  AtomicDomainRef domain;
  std::uint64_t revision, published_version;
  bool operator==(const PublishedCommit &) const = default;
};
class PublicationProof : public PortLifetime {
protected:
  PublicationProof() = default;
};
class PublicationAuthorityPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<const PublicationProof>>
  attest(const PublishedCommit &) = 0;
  virtual Result<void> validate(const PublicationProof &,
                                const PublishedCommit &) const = 0;
};
enum class RequiredRecordState { NotRequired, Pending, Recorded, Failed };
enum class RepairKind { CommitLedger, ReceiptReconcile, ManualReview };
struct RecordFailure {
  Error reason;
  bool writes_blocked;
  RepairKind repair;
};
enum class ApplyDecision { NotReached, CancelWon, ClaimWon };
struct BeforeApplyDecision {
  bool business_entered;
  ApplyDecision decision;
  bool no_application_proven;
  bool operator==(const BeforeApplyDecision &) const = default;
};
struct FinalizationFacts {
  RequiredRecordState record_state;
  std::uint64_t local_work_remaining, required_children_unsettled;
  bool operator==(const FinalizationFacts &) const = default;
};
struct OutcomeConditions {
  std::optional<BeforeApplyDecision> before_apply;
  std::optional<RecordFailure> record_failure;
  FinalizationFacts finalization;
};
inline bool same_error(const Error &a, const Error &b) {
  return a.code() == b.code() && bool(a.info()) == bool(b.info()) &&
         (!a.info() || a.info()->text() == b.info()->text());
}
inline Result<void> validate_outcome_conditions(const OutcomeConditions &a,
                                                const OutcomeConditions &b) {
  if (a.before_apply != b.before_apply || a.finalization != b.finalization ||
      a.record_failure.has_value() != b.record_failure.has_value())
    return reject(ContractsErrc::InvalidProof);
  if (a.record_failure) {
    const auto &x = *a.record_failure;
    const auto &y = *b.record_failure;
    if (!same_error(x.reason, y.reason) ||
        x.writes_blocked != y.writes_blocked || x.repair != y.repair)
      return reject(ContractsErrc::InvalidProof);
  }
  return {};
}
struct OutcomeValidation {
  PublicationAuthorityPort &publication;
  std::span<const std::shared_ptr<const PublicationProof>> proofs;
  FactBudget budget;
};
namespace detail {
inline std::vector<FactId> unresolved(const KnownFacts &fs) {
  std::vector<FactId> result;
  for (const auto &f : fs.values())
    if (auto u = std::get_if<UnknownFact>(&f)) {
      bool resolved = false;
      for (const auto &x : fs.values())
        if (auto r = std::get_if<ResolutionRecord>(&x);
            r && r->unknown_id == u->fact_id)
          resolved = true;
      if (!resolved)
        result.push_back(u->fact_id);
    }
  return result;
}
// D1.05 热路径使用这两个只读遍历，避免 Debug STL 临时 vector 的分配；
// 保留 unresolved() 给已有非热路径消费者使用。
inline bool is_unresolved(const KnownFacts &fs, FactId id) noexcept {
  for (const auto &f : fs.values())
    if (auto unknown = std::get_if<UnknownFact>(&f); unknown && unknown->fact_id == id) {
      for (const auto &other : fs.values())
        if (auto resolution = std::get_if<ResolutionRecord>(&other);
            resolution && resolution->unknown_id == id)
          return false;
      return true;
    }
  return false;
}
inline std::size_t count_unresolved(const KnownFacts &fs) noexcept {
  std::size_t count = 0;
  for (const auto &fact : fs.values())
    if (auto unknown = std::get_if<UnknownFact>(&fact);
        unknown && is_unresolved(fs, unknown->fact_id))
      ++count;
  return count;
}
inline bool applied(const KnownFacts &fs) {
  for (const auto &f : fs.values()) {
    if (std::holds_alternative<CommitFact>(f) ||
        std::holds_alternative<LifecycleFact>(f))
      return true;
    if (auto e = std::get_if<EffectFact>(&f);
        e && e->application != Application::NotApplied)
      return true;
  }
  return false;
}
inline Result<void> steps(std::span<const StepSummary> ss, bool all,
                          FactBudget budget) {
  if (ss.empty() || ss.size() > budget.max_steps)
    return reject(ContractsErrc::InvalidFact);
  bool failed = false;
  for (std::size_t i = 0; i < ss.size(); ++i) {
    const auto &s = ss[i];
    if (s.status > StepStatus::Cancelled || s.actual > ChildResult::Unknown)
      return reject(ContractsErrc::InvalidFact);
    for (std::size_t j = 0; j < i; ++j)
      if (ss[j].step_id == s.step_id)
        return reject(ContractsErrc::InvalidFact);
    if (s.required) {
      if (s.status != StepStatus::Succeeded)
        failed = true;
      if (all && (s.status != StepStatus::Succeeded ||
                  s.actual != ChildResult::Succeeded || !s.finalized))
        return reject(ContractsErrc::InvalidFact);
      if (s.actual == ChildResult::Unknown)
        return reject(ContractsErrc::InvalidFact);
    }
  }
  if (!all && !failed)
    return reject(ContractsErrc::InvalidFact);
  return {};
}
template <class R> Result<void> value_valid(const Result<R> &r, bool required) {
  if (!r)
    return required ? reject(ContractsErrc::InvalidContract) : Result<void>{};
  if constexpr (!std::same_as<R, void>)
    return TypeContract<R>::validate(*r);
  else
    return {};
}
} // namespace detail
template <ContractResult R> class Outcome final {
public:
  using Candidate =
      std::variant<ReadCompleted<R>, StateCommitted<R>, EffectResolved<R>,
                   LifecycleResolved<R>, PlanCompleted<R>, FailedBeforeApply,
                   CancelledBeforeApply, PartialCompletion, Indeterminate>;
  Outcome(Outcome &&) = default;
  Outcome(const Outcome &) = default;
  Outcome &operator=(const Outcome &) = delete;
  Outcome &operator=(Outcome &&) = delete;
  static Result<Outcome> validate(Candidate candidate,
                                  std::shared_ptr<const KnownFacts> facts,
                                  EvidenceState evidence,
                                  const OutcomeConditions &c,
                                  const OutcomeValidation &v) {
    if (!facts)
      return make_unexpected(error(ContractsErrc::InvalidFact));
    auto checked = verify(candidate, *facts, evidence, c, v);
    if (!checked)
      return make_unexpected(checked.error());
    // 深复制标准序列，消除输入容器元素的可变别名；R遵守其受审拥有合同。
    std::visit(
        [](auto &x) {
          using X = std::decay_t<decltype(x)>;
          if constexpr (std::same_as<X, PlanCompleted<R>> ||
                        std::same_as<X, PartialCompletion>) {
            auto clone = x.steps;
            x.steps.swap(clone);
          } else if constexpr (std::same_as<X, EffectResolved<R>>) {
            auto clone = x.report.external_evidence;
            x.report.external_evidence.swap(clone);
          } else if constexpr (std::same_as<X, Indeterminate>) {
            auto clone = x.unknown_ids;
            x.unknown_ids.swap(clone);
          }
        },
        candidate);
    return Outcome{std::move(candidate), std::move(facts), evidence, c};
  }
  const Candidate &value() const noexcept { return value_; }
  const KnownFacts &facts() const noexcept { return *facts_; }
  const std::shared_ptr<const KnownFacts> &facts_owner() const noexcept {
    return facts_;
  }
  EvidenceState evidence() const noexcept { return evidence_; }
  const OutcomeConditions &conditions() const noexcept { return conditions_; }
  // 消费已有验证结果，只追加必要记录状态；不复制或重新验证业务 R。
  Result<Outcome> with_required_record(RequiredRecordState next,
                                      std::optional<RecordFailure> failure={}) && {
    const auto previous=conditions_.finalization.record_state;
    if(!((previous==RequiredRecordState::NotRequired&&next==RequiredRecordState::Pending)||
         (previous==RequiredRecordState::Pending&&
          (next==RequiredRecordState::Recorded||next==RequiredRecordState::Failed))))
      return make_unexpected(error(ContractsErrc::InvalidPhase));
    if((next==RequiredRecordState::Failed)!=failure.has_value())
      return make_unexpected(error(ContractsErrc::InvalidFact));
    if(failure) {
      bool durable=false,effect=false;
      for(const auto& fact:facts_->values()) {
        if(auto commit=std::get_if<CommitFact>(&fact))durable|=commit->durability==CommitDurability::DurableCommitted;
        effect|=std::holds_alternative<EffectFact>(fact)||std::holds_alternative<UnknownFact>(fact);
      }
      auto repair=durable?RepairKind::CommitLedger:effect?RepairKind::ReceiptReconcile:RepairKind::ManualReview;
      if(!failure->writes_blocked||!failure->reason.code().value()||failure->repair!=repair)
        return make_unexpected(error(ContractsErrc::InvalidFact));
    }
    auto conditions=conditions_;conditions.finalization.record_state=next;
    conditions.record_failure=std::move(failure);
    return Outcome{std::move(value_),std::move(facts_),
        next==RequiredRecordState::Failed?EvidenceState::RequiredRecordFailed:evidence_,std::move(conditions)};
  }
  Result<void> revalidate(const OutcomeValidation &v,
                          const OutcomeConditions &expected,
                          const KnownFacts &previous) const {
    auto c = validate_outcome_conditions(conditions_, expected);
    if (!c)
      return c;
    auto f = validate_facts_append(previous, *facts_);
    if (!f)
      return f;
    return verify(value_, *facts_, evidence_, expected, v);
  }

private:
  Outcome(Candidate v, std::shared_ptr<const KnownFacts> fs, EvidenceState e,
          OutcomeConditions c)
      : value_(std::move(v)), facts_(std::move(fs)), evidence_(e),
        conditions_(std::move(c)) {}
  static Result<void> verify(const Candidate &value, const KnownFacts &fs,
                             EvidenceState e, const OutcomeConditions &c,
                             const OutcomeValidation &v) {
    if (e > EvidenceState::PersistenceUncertain ||
        c.finalization.record_state > RequiredRecordState::Failed)
      return reject(ContractsErrc::InvalidFact);
    auto fv = detail::validate_facts(fs.values(), v.budget);
    if (!fv)
      return fv;
    bool durable = false;
    for (const auto &f : fs.values())
      if (auto commit = std::get_if<CommitFact>(&f);
          commit && commit->durability == CommitDurability::DurableCommitted)
        durable = true;
    if (durable && e == EvidenceState::Volatile)
      return reject(ContractsErrc::InvalidFact);
    if (e == EvidenceState::PersistenceUncertain &&
        !std::holds_alternative<Indeterminate>(value))
      return reject(ContractsErrc::InvalidFact);
    bool failed = c.finalization.record_state == RequiredRecordState::Failed;
    if (failed != (e == EvidenceState::RequiredRecordFailed) ||
        failed != c.record_failure.has_value())
      return reject(ContractsErrc::InvalidFact);
    if (failed) {
      const auto &f = *c.record_failure;
      if (!f.writes_blocked || f.reason.code().value() == 0 ||
          f.repair > RepairKind::ManualReview)
        return reject(ContractsErrc::InvalidFact);
      bool effect = false;
      for (const auto &x : fs.values())
        if (std::holds_alternative<EffectFact>(x) ||
            std::holds_alternative<UnknownFact>(x))
          effect = true;
      auto expected = durable  ? RepairKind::CommitLedger
                      : effect ? RepairKind::ReceiptReconcile
                               : RepairKind::ManualReview;
      if (f.repair != expected)
        return reject(ContractsErrc::InvalidFact);
    }
    for (const auto &f : fs.values())
      if (auto published = std::get_if<PublishedFact>(&f)) {
        const CommitFact *commit = nullptr;
        for (const auto &x : fs.values())
          if (auto cf = std::get_if<CommitFact>(&x);
              cf && cf->commit == published->commit)
            commit = cf;
        PublishedCommit expected{commit->commit, commit->domain,
                                 commit->revision,
                                 published->published_version};
        bool proven = false;
        for (const auto &proof : v.proofs)
          if (proof && v.publication.validate(*proof, expected))
            proven = true;
        if (!proven)
          return reject(ContractsErrc::InvalidProof);
      }
    const auto unknown_count = detail::count_unresolved(fs);
    bool has_applied = detail::applied(fs);
    if (c.before_apply && (c.before_apply->decision > ApplyDecision::ClaimWon ||
                           (c.before_apply->no_application_proven &&
                            (has_applied || unknown_count != 0))))
      return reject(ContractsErrc::InvalidProof);
    return std::visit(
        [&](const auto &x) -> Result<void> {
          using X = std::decay_t<decltype(x)>;
          if constexpr (std::same_as<X, ReadCompleted<R>>) {
            if (has_applied || unknown_count != 0 ||
                x.scope > ResultScope::Candidate)
              return reject(ContractsErrc::InvalidFact);
            return detail::value_valid(x.result, true);
          } else if constexpr (std::same_as<X, StateCommitted<R>>) {
            std::size_t commits = 0, published = 0;
            for (const auto &f : fs.values()) {
              if (auto cf = std::get_if<CommitFact>(&f)) {
                ++commits;
                if (cf->commit != x.commit || cf->domain != x.domain ||
                    cf->revision != x.revision)
                  return reject(ContractsErrc::InvalidFact);
              }
              if (auto pf = std::get_if<PublishedFact>(&f)) {
                ++published;
                if (pf->commit != x.commit ||
                    pf->published_version != x.published_version)
                  return reject(ContractsErrc::InvalidFact);
              }
            }
            if (commits != 1 || published != 1 || unknown_count != 0)
              return reject(ContractsErrc::InvalidFact);
            return detail::value_valid(x.result, true);
          } else if constexpr (std::same_as<X, EffectResolved<R>>) {
            const auto &r = x.report;
            if (!r.application ||
                *r.application > Application::PartiallyApplied ||
                r.status > BusinessStatus::Failed ||
                r.external_evidence.empty() || unknown_count != 0)
              return reject(ContractsErrc::InvalidFact);
            std::size_t bytes = 0;
            for (const auto &text : r.external_evidence) {
              if (!detail::good_text(text) ||
                  text.size() > v.budget.max_text_bytes - bytes)
                return reject(ContractsErrc::BudgetExceeded);
              bytes += text.size();
            }
            bool found = false;
            for (const auto &f : fs.values())
              if (auto ef = std::get_if<EffectFact>(&f);
                  ef && ef->effect == r.effect) {
                if (ef->application != *r.application)
                  return reject(ContractsErrc::InvalidFact);
                found = true;
              }
            if (!found)
              return reject(ContractsErrc::InvalidFact);
            return detail::value_valid(r.result, false);
          } else if constexpr (std::same_as<X, LifecycleResolved<R>>) {
            const auto &r = x.report;
            if (r.target.empty() || r.status > BusinessStatus::Failed ||
                unknown_count != 0)
              return reject(ContractsErrc::InvalidFact);
            bool found = false;
            for (const auto &f : fs.values())
              if (auto lf = std::get_if<LifecycleFact>(&f);
                  lf && lf->transition == r.transition) {
                if (lf->before != r.before || lf->after != r.after ||
                    lf->generation != r.generation)
                  return reject(ContractsErrc::InvalidFact);
                found = true;
              }
            if (!found)
              return reject(ContractsErrc::InvalidFact);
            return detail::value_valid(r.result, false);
          } else if constexpr (std::same_as<X, PlanCompleted<R>>) {
            if (unknown_count != 0 || c.finalization.required_children_unsettled)
              return reject(ContractsErrc::NotQuiescent);
            auto valid = detail::steps(x.steps, true, v.budget);
            if (!valid)
              return valid;
            return detail::value_valid(x.exports, true);
          } else if constexpr (std::same_as<X, FailedBeforeApply> ||
                               std::same_as<X, CancelledBeforeApply>) {
            if (!c.before_apply || !c.before_apply->no_application_proven ||
                has_applied || unknown_count != 0 || x.reason.code().value() == 0)
              return reject(ContractsErrc::InvalidProof);
            if constexpr (std::same_as<X, FailedBeforeApply>) {
              if (!c.before_apply->business_entered)
                return reject(ContractsErrc::InvalidProof);
            } else if (c.before_apply->decision != ApplyDecision::CancelWon)
              return reject(ContractsErrc::InvalidProof);
            return {};
          } else if constexpr (std::same_as<X, PartialCompletion>) {
            if (!has_applied || unknown_count != 0)
              return reject(ContractsErrc::InvalidFact);
            return detail::steps(x.steps, false, v.budget);
          } else {
            if (unknown_count == 0 || x.unknown_ids.size() != unknown_count)
              return reject(ContractsErrc::InvalidFact);
            for (std::size_t i = 0; i < x.unknown_ids.size(); ++i) {
              if (!detail::is_unresolved(fs, x.unknown_ids[i]))
                return reject(ContractsErrc::InvalidFact);
              for (std::size_t j = 0; j < i; ++j)
                if (x.unknown_ids[i] == x.unknown_ids[j])
                  return reject(ContractsErrc::InvalidFact);
            }
            return {};
          }
        },
        value);
  }
  Candidate value_;
  std::shared_ptr<const KnownFacts> facts_;
  EvidenceState evidence_;
  OutcomeConditions conditions_;
};
enum class AcceptanceGuarantee { Volatile, DurableAccepted };
struct Rejected {
  Error reason;
};
struct Accepted {
  ExecutionRef execution;
  AcceptanceGuarantee guarantee;
};
using SubmitReply = std::variant<Rejected, Accepted>;
template <class R> struct Completed {
  Outcome<R> outcome;
};
template <class R> using InvokeReply = std::variant<Rejected, Completed<R>>;
struct PhaseConditions {
  FinalizationFacts finalization;
  std::optional<RecordFailure> record_failure;
  std::optional<ExecutionPhase> resume_target;
  bool recoverable;
  bool children_transferred;
};
inline Result<void> validate_phase_change(ExecutionPhase from,
                                          ExecutionPhase to,
                                          const PhaseConditions &c) {
  if (from > ExecutionPhase::Terminal || to > ExecutionPhase::Terminal ||
      from == ExecutionPhase::Terminal)
    return reject(ContractsErrc::InvalidPhase);
  if (to == ExecutionPhase::Terminal) {
    if (from != ExecutionPhase::Finalizing ||
        c.finalization.local_work_remaining ||
        (!c.children_transferred &&
         c.finalization.required_children_unsettled) ||
        c.finalization.record_state == RequiredRecordState::Pending)
      return reject(ContractsErrc::NotQuiescent);
    if ((c.finalization.record_state == RequiredRecordState::Failed) !=
        c.record_failure.has_value())
      return reject(ContractsErrc::InvalidFact);
    if (c.record_failure && (!c.record_failure->writes_blocked ||
                             !c.record_failure->reason.code().value()))
      return reject(ContractsErrc::InvalidFact);
    return {};
  }
  if (from == ExecutionPhase::Suspended) {
    if (!c.recoverable || !c.resume_target || *c.resume_target != to ||
        to > ExecutionPhase::WaitingChild)
      return reject(ContractsErrc::InvalidPhase);
    return {};
  }
  if (to == ExecutionPhase::Suspended) {
    if (!c.recoverable || c.finalization.local_work_remaining ||
        from == ExecutionPhase::Finalizing)
      return reject(ContractsErrc::InvalidPhase);
    return {};
  }
  bool allowed =
      (from == ExecutionPhase::Queued &&
       (to == ExecutionPhase::WaitingResources ||
        to == ExecutionPhase::Running || to == ExecutionPhase::Finalizing)) ||
      (from == ExecutionPhase::WaitingResources &&
       (to == ExecutionPhase::Running || to == ExecutionPhase::Finalizing)) ||
      (from == ExecutionPhase::Running && (to == ExecutionPhase::WaitingChild ||
                                           to == ExecutionPhase::Finalizing)) ||
      (from == ExecutionPhase::WaitingChild &&
       (to == ExecutionPhase::Running || to == ExecutionPhase::Finalizing));
  return allowed ? Result<void>{} : reject(ContractsErrc::InvalidPhase);
}
} // namespace ock::contracts
