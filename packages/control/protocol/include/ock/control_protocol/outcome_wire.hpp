#pragma once
#include <ock/contracts/outcome.hpp>
#include <ock/control_protocol/observation_wire.hpp>
namespace ock::control {
namespace outcome_wire {
void text(data::PayloadBuilder &, std::string_view, std::string_view);
void error(data::PayloadBuilder &, std::string_view, const foundation::Error &);
void domain(data::PayloadBuilder &, const contracts::AtomicDomainRef &);
void facts(data::PayloadBuilder &, const contracts::KnownFacts &);
void conditions(data::PayloadBuilder &, const contracts::OutcomeConditions &);
void steps(data::PayloadBuilder &, std::span<const contracts::StepSummary>);
Result<void> value(data::PayloadBuilder &, data::ValueView);
template <class Id>
void identity(data::PayloadBuilder &b, std::string_view key, const Id &id) {
  text(b, key, wire_text(id));
}
inline void count(data::PayloadBuilder &b, std::string_view key,
                  std::uint64_t n) {
  text(b, key, std::to_string(n));
}
inline std::string_view application(contracts::Application a) {
  constexpr std::string_view names[] = {"NotApplied", "Applied",
                                        "PartiallyApplied"};
  return names[static_cast<unsigned>(a)];
}
template <class R, class Encoder>
void result(data::PayloadBuilder &b, const Result<R> &r, Encoder &encode,
            std::string_view key = "result") {
  if (!r) {
    error(b, std::string(key) + "_error", r.error());
    return;
  }
  if constexpr (std::is_void_v<R>) {
    (void)b.key(key);
    if (key == "exports") { (void)b.begin_object(); (void)b.end_object(); }
    else (void)b.null();
  } else {
    // 编码失败属于结果材料失败，不修改已经验证的 Outcome 和 known_facts。
    auto encoded = [&]() -> Result<data::Payload> {
      try {
        return encode(*r);
      } catch (...) {
        return foundation::make_unexpected(
            data::error(data::DataErrc::InvalidState));
      }
    }();
    if (!encoded) {
      error(b, std::string(key) + "_error", encoded.error());
      return;
    }
    if (key == "exports" && encoded->view().kind() != data::Kind::Object) {
      error(b, "exports_error", data::error(data::DataErrc::WrongType));
      return;
    }
    (void)b.key(key);
    (void)value(b, encoded->view());
  }
}
} // namespace outcome_wire
inline Result<data::Payload> encode_submit(const contracts::SubmitReply& reply,data::Budget budget={}) {
  data::PayloadBuilder b(budget);(void)b.begin_object();
  if(auto rejected=std::get_if<contracts::Rejected>(&reply)) {
    outcome_wire::text(b,"kind","Rejected");outcome_wire::error(b,"reason",rejected->reason);
  } else {
    const auto& accepted=std::get<contracts::Accepted>(reply);
    if(accepted.execution.execution_id.empty()||accepted.guarantee>contracts::AcceptanceGuarantee::DurableAccepted)
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
    outcome_wire::text(b,"kind","Accepted");(void)b.key("execution_ref");(void)b.begin_object();
    outcome_wire::identity(b,"execution_id",accepted.execution.execution_id);(void)b.end_object();
    outcome_wire::text(b,"acceptance_guarantee",accepted.guarantee==contracts::AcceptanceGuarantee::Volatile?"Volatile":"DurableAccepted");
  }
  (void)b.end_object();return b.freeze();
}
template <class R, class Encoder>
Result<data::Payload> encode_invoke(const contracts::InvokeReply<R> &reply,
                                    Encoder encode, data::Budget budget = {}) {
  using namespace contracts;
  namespace w = outcome_wire;
  data::PayloadBuilder b(budget);
  (void)b.begin_object();
  if (auto rejected = std::get_if<Rejected>(&reply)) {
    w::text(b, "kind", "Rejected");
    w::error(b, "reason", rejected->reason);
  } else {
    w::text(b, "kind", "Completed");
    (void)b.key("outcome");
    (void)b.begin_object();
    const auto &outcome = std::get<Completed<R>>(reply).outcome;
    constexpr std::string_view evidence[] = {
        "Volatile", "Durable", "RequiredRecordFailed", "PersistenceUncertain"};
    w::text(b, "evidence", evidence[static_cast<unsigned>(outcome.evidence())]);
    w::facts(b, outcome.facts());
    w::conditions(b, outcome.conditions());
    std::visit(
        [&](const auto &x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::same_as<T, ReadCompleted<R>>) {
            w::text(b, "kind", "ReadCompleted");
            w::text(b, "result_scope",
                    x.scope == ResultScope::ReadOnly ? "ReadOnly"
                                                     : "Candidate");
            w::result(b, x.result, encode);
          } else if constexpr (std::same_as<T, StateCommitted<R>>) {
            w::text(b, "kind", "StateCommitted");
            w::identity(b, "commit_id", x.commit);
            w::domain(b, x.domain);
            w::count(b, "revision", x.revision);
            w::count(b, "published_version", x.published_version);
            w::result(b, x.result, encode);
          } else if constexpr (std::same_as<T, EffectResolved<R>>) {
            w::text(b, "kind", "EffectResolved");
            auto &r = x.report;
            w::identity(b, "effect_id", r.effect);
            if (r.application)
              w::text(b, "application", w::application(*r.application));
            else {
              (void)b.key("application");
              (void)b.null();
            }
            w::text(b, "status",
                    r.status == BusinessStatus::Succeeded ? "Succeeded"
                                                          : "Failed");
            (void)b.key("external_evidence");
            (void)b.begin_array();
            for (auto &e : r.external_evidence)
              (void)b.string(e);
            (void)b.end_array();
            w::text(b, "reconcile", r.reconcile);
            w::result(b, r.result, encode);
          } else if constexpr (std::same_as<T, LifecycleResolved<R>>) {
            w::text(b, "kind", "LifecycleResolved");
            auto &r = x.report;
            w::identity(b, "transition_id", r.transition);
            w::identity(b, "target", r.target);
            w::text(b, "before", r.before.view());
            w::text(b, "after", r.after.view());
            w::count(b, "generation", r.generation);
            w::text(b, "status",
                    r.status == BusinessStatus::Succeeded ? "Succeeded"
                                                          : "Failed");
            w::result(b, r.result, encode);
          } else if constexpr (std::same_as<T, PlanCompleted<R>>) {
            w::text(b, "kind", "PlanCompleted");
            w::result(b, x.exports, encode, "exports");
            w::steps(b, x.steps);
          } else if constexpr (std::same_as<T, FailedBeforeApply>) {
            w::text(b, "kind", "FailedBeforeApply");
            w::text(b, "failure_phase", x.failure_phase.view());
            w::error(b, "reason", x.reason);
            w::text(b, "proof", "NoAppliedStateOrEffect");
          } else if constexpr (std::same_as<T, CancelledBeforeApply>) {
            w::text(b, "kind", "CancelledBeforeApply");
            w::error(b, "reason", x.reason);
            w::text(b, "proof", "NoAppliedStateOrEffect");
          } else if constexpr (std::same_as<T, PartialCompletion>) {
            w::text(b, "kind", "PartialCompletion");
            w::steps(b, x.steps);
          } else {
            w::text(b, "kind", "Indeterminate");
            (void)b.key("unknown_ids");
            (void)b.begin_array();
            for (auto id : x.unknown_ids)
              (void)b.string(wire_text(id));
            (void)b.end_array();
          }
        },
        outcome.value());
    (void)b.end_object();
  }
  (void)b.end_object();
  return b.freeze();
}
} // namespace ock::control
