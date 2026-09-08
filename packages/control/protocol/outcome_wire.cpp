#include <ock/control_protocol/outcome_wire.hpp>
namespace ock::control::outcome_wire {
void text(data::PayloadBuilder &b, std::string_view key,
          std::string_view text) {
  (void)b.key(key);
  (void)b.string(text);
}
void error(data::PayloadBuilder &b, std::string_view key,
           const foundation::Error &e) {
  (void)b.key(key);
  (void)b.begin_object();
  text(b, "domain", e.code().domain().name());
  (void)b.key("code");
  (void)b.uint64(e.code().value());
  if (e.info())
    text(b, "message", e.info()->text());
  (void)b.end_object();
}
void domain(data::PayloadBuilder &b, const contracts::AtomicDomainRef &d) {
  (void)b.key("domain");
  (void)b.begin_object();
  text(b, "provider", d.provider.view());
  identity(b, "domain_id", d.domain_id);
  identity(b, "generation", d.generation);
  (void)b.end_object();
}
void facts(data::PayloadBuilder &b, const contracts::KnownFacts &fs) {
  using namespace contracts;
  (void)b.key("known_facts");
  (void)b.begin_array();
  for (const auto &fact : fs.values())
    std::visit(
        [&](const auto &f) {
          using T = std::decay_t<decltype(f)>;
          (void)b.begin_object();
          identity(b, "fact_id", f.fact_id);
          if constexpr (std::same_as<T, CommitFact>) {
            text(b, "kind", "CommitFact");
            identity(b, "commit_id", f.commit);
            domain(b, f.domain);
            count(b, "revision", f.revision);
            text(b, "durability",
                 f.durability == CommitDurability::Memory ? "Memory"
                                                          : "DurableCommitted");
          } else if constexpr (std::same_as<T, PublishedFact>) {
            text(b, "kind", "PublishedFact");
            identity(b, "commit_id", f.commit);
            count(b, "published_version", f.published_version);
          } else if constexpr (std::same_as<T, EffectFact>) {
            text(b, "kind", "EffectFact");
            identity(b, "effect_id", f.effect);
            text(b, "application", application(f.application));
          } else if constexpr (std::same_as<T, LifecycleFact>) {
            text(b, "kind", "LifecycleFact");
            identity(b, "transition_id", f.transition);
            text(b, "before", f.before.view());
            text(b, "after", f.after.view());
            count(b, "generation", f.generation);
          } else if constexpr (std::same_as<T, UnknownFact>) {
            text(b, "kind", "UnknownFact");
            text(b, "boundary",
                 f.boundary == UnknownBoundary::ExternalEffect
                     ? "ExternalEffect"
                     : "StorageCommit");
            std::visit([&](const auto &id) { identity(b, "reference_id", id); },
                       f.reference);
            text(b, "reconcile", f.reconcile);
          } else {
            text(b, "kind", "ResolutionRecord");
            identity(b, "unknown_id", f.unknown_id);
            text(b, "determination",
                 f.determination == Determination::Applied ? "Applied"
                                                           : "NotApplied");
            text(b, "evidence_ref", f.evidence_ref);
          }
          (void)b.end_object();
        },
        fact);
  (void)b.end_array();
}
void conditions(data::PayloadBuilder &b,
                const contracts::OutcomeConditions &c) {
  using namespace contracts;
  (void)b.key("conditions");
  (void)b.begin_object();
  constexpr std::string_view states[] = {"NotRequired", "Pending", "Recorded",
                                         "Failed"};
  text(b, "record_state",
       states[static_cast<unsigned>(c.finalization.record_state)]);
  count(b, "local_work_remaining", c.finalization.local_work_remaining);
  count(b, "required_children_unsettled",
        c.finalization.required_children_unsettled);
  if (c.before_apply) {
    (void)b.key("before_apply");
    (void)b.begin_object();
    (void)b.key("business_entered");
    (void)b.boolean(c.before_apply->business_entered);
    constexpr std::string_view decisions[] = {"NotReached", "CancelWon",
                                              "ClaimWon"};
    text(b, "decision",
         decisions[static_cast<unsigned>(c.before_apply->decision)]);
    (void)b.key("no_application_proven");
    (void)b.boolean(c.before_apply->no_application_proven);
    (void)b.end_object();
  }
  if (c.record_failure) {
    error(b, "record_error", c.record_failure->reason);
    (void)b.key("writes_blocked");
    (void)b.boolean(c.record_failure->writes_blocked);
    constexpr std::string_view repairs[] = {"CommitLedger", "ReceiptReconcile",
                                            "ManualReview"};
    text(b, "repair", repairs[static_cast<unsigned>(c.record_failure->repair)]);
  }
  (void)b.end_object();
}
void steps(data::PayloadBuilder &b,
           std::span<const contracts::StepSummary> steps) {
  (void)b.key("steps");
  (void)b.begin_array();
  constexpr std::string_view status[] = {"Succeeded", "Failed", "Cancelled"};
  constexpr std::string_view actual[] = {"Succeeded", "Failed", "Unknown"};
  for (auto &s : steps) {
    (void)b.begin_object();
    text(b, "step_id", s.step_id.view());
    text(b, "status", status[static_cast<unsigned>(s.status)]);
    text(b, "actual", actual[static_cast<unsigned>(s.actual)]);
    (void)b.key("required");
    (void)b.boolean(s.required);
    (void)b.key("finalized");
    (void)b.boolean(s.finalized);
    (void)b.end_object();
  }
  (void)b.end_array();
}
Result<void> value(data::PayloadBuilder &b, data::ValueView v) {
  using data::Kind;
  switch (v.kind()) {
  case Kind::Null:
    return b.null();
  case Kind::Boolean:
    return b.boolean(*v.boolean());
  case Kind::Int64:
    return b.int64(*v.int64());
  case Kind::UInt64:
    return b.uint64(*v.uint64());
  case Kind::Number:
    return b.number(*v.number());
  case Kind::String:
    return b.string(*v.string());
  case Kind::Array:
  case Kind::Object: {
    const bool object = v.kind() == Kind::Object;
    auto check = object ? b.begin_object() : b.begin_array();
    if (!check)
      return check;
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (object) {
        check = b.key(*v.key_at(i));
        if (!check)
          return check;
      }
      check = value(b, object ? v.at(*v.key_at(i)) : v.at(i));
      if (!check)
        return check;
    }
    return object ? b.end_object() : b.end_array();
  }
  default:
    return foundation::make_unexpected(
        data::error(data::DataErrc::InvalidState));
  }
}
} // namespace ock::control::outcome_wire
