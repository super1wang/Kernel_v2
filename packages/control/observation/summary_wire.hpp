#pragma once
#include <ock/control_protocol/outcome_wire.hpp>
namespace ock::control::detail {
inline void summary_fields(data::PayloadBuilder& b,const contracts::SummaryInput& summary,bool full_result_available) {
  using outcome_wire::text;
  text(b, "projection", "summary");
  (void)b.key("full_result_available");
  (void)b.boolean(full_result_available);
  (void)b.key("execution_ref");
  (void)b.begin_object();
  text(b, "execution_id", wire_text(summary.execution.execution_id));
  (void)b.end_object();
  text(b, "host_incarnation", wire_text(summary.host));
  constexpr std::string_view phases[] = {
      "Queued",     "WaitingResources", "Running", "WaitingChild",
      "Finalizing", "Suspended",        "Terminal"};
  text(b, "phase", phases[static_cast<unsigned>(summary.phase)]);
  text(b, "observation_version", std::to_string(summary.version.value()));
  constexpr std::string_view evidence[]={"Volatile","Durable","RequiredRecordFailed","PersistenceUncertain"};
  constexpr std::string_view records[]={"NotRequired","Pending","Recorded","Failed"};
  constexpr std::string_view repairs[]={"CommitLedger","ReceiptReconcile","ManualReview"};
  text(b,"evidence",evidence[static_cast<unsigned>(summary.evidence)]);
  text(b,"record_state",records[static_cast<unsigned>(summary.record_state)]);
  (void)b.key("writes_blocked");(void)b.boolean(summary.writes_blocked);
  if(summary.fault)outcome_wire::error(b,"fault",*summary.fault);
  if(summary.repair)text(b,"repair",repairs[static_cast<unsigned>(*summary.repair)]);
  (void)b.key("progress");
  (void)b.begin_object();
  text(b, "completed", std::to_string(summary.progress.completed));
  text(b, "total", std::to_string(summary.progress.total));
  text(b,"scope",summary.progress.scope==contracts::ResultScope::Candidate?"Candidate":"ReadOnly");
  (void)b.end_object();
  (void)b.key("fact_summaries");
  (void)b.begin_array();
  constexpr std::string_view kinds[] = {"Commit",  "Published",
                                        "Effect",  "Lifecycle",
                                        "Unknown", "Resolution"};
  for (const auto &fact : summary.facts) {
    (void)b.begin_object();
    text(b, "fact_id", wire_text(fact.fact));
    text(b, "kind", kinds[static_cast<unsigned>(fact.kind)]);
    if (fact.reference)
      std::visit(
          [&](const auto &ref) {
            using Id = std::decay_t<decltype(ref)>;
            constexpr auto kind =
                std::same_as<Id, contracts::CommitId>       ? "Commit"
                : std::same_as<Id, contracts::EffectId>     ? "Effect"
                : std::same_as<Id, contracts::TransitionId> ? "Transition"
                                                            : "Fact";
            text(b, "reference_kind", kind);
            text(b, "reference", wire_text(ref));
          },
          *fact.reference);
    // Unknown 的事实是未决，不将摘要占位值写成 NotApplied 结论。
    if (fact.kind != contracts::FactKind::Unknown) {
      constexpr std::string_view applications[] = {"NotApplied", "Applied",
                                                   "PartiallyApplied"};
      text(b, "application",
           applications[static_cast<unsigned>(fact.application)]);
    }
    (void)b.end_object();
  }
  (void)b.end_array();
}
}
