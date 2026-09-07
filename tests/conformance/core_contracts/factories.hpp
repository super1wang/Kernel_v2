#pragma once
// D1.02确定性工厂：只见证公开端口规则，不提供生产运行服务。
inline EffectReport<int> effect_sample() {
  return {id<EffectId>(),
          Application::Applied,
          BusinessStatus::Failed,
          {"receipt"},
          3,
          {}};
}
inline EffectFact effect_fact() {
  return {id<FactId>(), id<EffectId>(), Application::Applied};
}
inline EffectReport<int> effect_handler(const int &, EffectContext &) {
  return effect_sample();
}
inline TransitionReport<int> transition_sample() {
  return {id<TransitionId>(),
          id<foundation::ObjectId>(),
          name("Ready"),
          name("Failed"),
          1,
          BusinessStatus::Failed,
          3};
}
inline LifecycleFact lifecycle_fact() {
  return {id<FactId>(), id<TransitionId>(), name("Ready"), name("Failed"), 1};
}
inline TransitionReport<int> transition_handler(const int &, TransitionView &) {
  return transition_sample();
}
inline UnknownFact unknown_fact() {
  return {id<FactId>(), UnknownBoundary::ExternalEffect, id<EffectId>(),
          "query receipt"};
}
inline PreparedIdentity prepared_identity() {
  return {id<CommitId>(), id<ReservationId>(), domain(), 0, 1};
}
inline RecordRequest record_request() {
  return {
      {id<foundation::TaskId>()}, {}, {}, name("receipt"), {std::byte{1}}, 1};
}
inline SummaryInput summary() {
  return {{id<foundation::TaskId>()},
          key(),
          {id<PrincipalId>()},
          {},
          ExecutionPhase::Finalizing,
          id<HostIncarnation>(),
          *ObservationVersion::create(1),
          {},
          {}};
}
struct WorkCounts {
  unsigned executed = 0, destroyed = 0, completed = 0, faults = 0;
  bool prepared = true;
  bool prepared_at_execution = false;
};
class CountReceiver : public CompletionPort {
public:
  unsigned count = 0;
  Result<void> candidate_ready(Result<void> status) noexcept override {
    ++count;
    return status;
  }
};
class WorkReceiver : public CompletionPort {
public:
  explicit WorkReceiver(std::shared_ptr<WorkCounts> c) : counts(std::move(c)) {}
  Result<void> candidate_ready(Result<void>) noexcept override {
    ++counts->completed;
    return {};
  }

private:
  std::shared_ptr<WorkCounts> counts;
};
class CountWork : public ReadyWork {
public:
  explicit CountWork(std::shared_ptr<WorkCounts> c, bool fault = false)
      : counts(std::move(c)), latch(std::make_shared<WorkReceiver>(counts)),
        fault_(fault) {}
  ~CountWork() override { ++counts->destroyed; }
  void execute() noexcept override {
    ++counts->executed;
    counts->prepared_at_execution = counts->prepared;
    try {
      if (fault_)
        throw std::runtime_error("work fault");
      (void)latch.complete({});
    } catch (...) {
      ++counts->faults;
      (void)latch.complete(reject(ContractsErrc::InvalidContract));
    }
  }

private:
  std::shared_ptr<WorkCounts> counts;
  CompletionLatch latch;
  bool fault_;
};
class InlineExecutor : public ExecutorPort {
public:
  Result<void> submit(std::unique_ptr<ReadyWork> work) override {
    work->execute();
    return {};
  }
};
class RejectExecutor : public ExecutorPort {
public:
  explicit RejectExecutor(bool throwing) : throw_(throwing) {}
  Result<void> submit(std::unique_ptr<ReadyWork>) override {
    if (throw_)
      throw std::runtime_error("submit rejected");
    return reject(ContractsErrc::Rejected);
  }

private:
  bool throw_;
};
class Authority final : public CallerAuthorityPort {
  class Grant final : public CallerGrant {
public:
  explicit Grant(const CallerDescription &d) : description_(d) {}
  const CallerDescription &description() const noexcept override {
    return description_;
  }

private:
  const CallerDescription description_;
};
  std::vector<std::shared_ptr<const Grant>> issued;
public:
  bool active = true;
  PrincipalRef authenticated_principal{id<PrincipalId>()};
  Result<std::shared_ptr<const CallerGrant>>
  authenticate(const CallerDescription &d) override {
    if (d.principal != authenticated_principal || d.delegated_by)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto g = std::make_shared<const Grant>(d);
    issued.push_back(g);
    return std::shared_ptr<const CallerGrant>(g);
  }
  Result<void> validate(const CallerGrant &g) const override {
    if (active)
      for (const auto &own : issued)
        if (own.get() == &g)
          return {};
    return reject(ContractsErrc::InvalidGrant);
  }
};
class RecordCounter : public RecordReceiver {
public:
  unsigned recorded = 0;
  void completed(RecordReport r) noexcept override {
    if (r.state == RequiredRecordState::Recorded)
      ++recorded;
  }
};
class RecordFactory : public RequiredRecordPort {
  class Reservation : public RecordReservation {
  public:
    Reservation(RecordFactory &o, std::uint64_t count)
        : owner(o), count_(count) {}
    ~Reservation() override { owner.available += count_; }
    RecordFactory &owner;
    std::uint64_t count_;
  };

public:
  explicit RecordFactory(std::uint64_t capacity) : available(capacity) {}
  std::uint64_t available;
  Result<std::unique_ptr<RecordReservation>>
  reserve(const RecordRequest &r) override {
    if (!r.capacity || r.capacity > available)
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    auto owned = std::make_unique<Reservation>(*this, r.capacity);
    available -= r.capacity;
    return std::unique_ptr<RecordReservation>(std::move(owned));
  }
  Result<void> record(std::unique_ptr<RecordReservation> reservation,
                      std::shared_ptr<const RecordRequestSnapshot> request,
                      std::shared_ptr<RecordReceiver> receiver) override {
    auto *real = dynamic_cast<Reservation *>(reservation.get());
    if (!real || &real->owner != this || !request || !receiver ||
        real->count_ != request->value().capacity)
      return reject(ContractsErrc::InvalidGrant);
    const auto &r = request->value();
    RecordReport report{
        r.execution, r.commit, r.effect, RequiredRecordState::Recorded, {}};
    auto checked =
        validate_record_report(r, RequiredRecordState::Pending, report);
    if (!checked)
      return checked;
    receiver->completed(report);
    return {};
  }
};
class HintCounter : public ObservationReceiver {
public:
  unsigned count = 0;
  void changed(ChangeHint) noexcept override { ++count; }
};
class ObservationFactory : public ObservationPort {
  struct Listener {
    bool active = true;
    std::shared_ptr<ObservationReceiver> receiver;
    CallerView caller;
    ObservationFilter filter;
  };
  class Lease : public ObservationLease {
  public:
    explicit Lease(std::shared_ptr<Listener> s) : state(std::move(s)) {}
    ~Lease() override { state->active = false; state->receiver.reset(); }

  private:
    std::shared_ptr<Listener> state;
  };

public:
  explicit ObservationFactory(std::shared_ptr<Authority> a)
      : authority_(std::move(a)) {}
  bool drop_hints = false;
  std::set<unsigned> hidden;
  std::map<std::uint64_t, std::shared_ptr<const ExecutionSummary>> entries;
  void add(const SummaryInput &s, std::uint64_t ordinal) {
    auto frozen = ExecutionSummary::create(s);
    CHECK(frozen);
    CHECK(ordinal != 0 && !entries.contains(ordinal));
    entries.emplace(ordinal, *frozen);
  }
  Result<std::shared_ptr<const ExecutionSummary>>
  get_summary(const CallerView &c, ExecutionRef ref) override {
    auto valid = validate_caller(*authority_, c);
    if (!valid)
      return make_unexpected(valid.error());
    for (const auto &[ordinal, s] : entries)
      if (s->value().execution == ref &&
          s->value().owner == c.description().principal &&
          !hidden.contains(static_cast<unsigned>(ordinal)))
        return s;
    return make_unexpected(error(ContractsErrc::InvalidGrant));
  }
  Result<ListPage> list_summaries(const CallerView &caller,
                                  const ListRequest &request) override {
    auto valid = validate_caller(*authority_, caller);
    if (!valid)
      return make_unexpected(valid.error());
    if (request.owner != caller.description().principal)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    ListPage page{id<HostIncarnation>(), {}, {}, name("retained")};
    auto budget = validate_list_page(request, page, {200, 2000});
    if (!budget)
      return make_unexpected(budget.error());
    std::uint64_t upper = request.position  ? request.position->upper_ordinal
                          : entries.empty() ? 0
                                            : entries.rbegin()->first;
    std::uint32_t scanned = 0;
    std::uint64_t last = 0;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      auto ordinal = it->first;
      if (ordinal > upper ||
          (request.position && ordinal >= request.position->before_ordinal))
        continue;
      if (scanned >= request.budget.scan_limit ||
          page.items.size() >= request.budget.page_size) {
        page.next = KeysetPosition{page.host, upper, last};
        break;
      }
      ++scanned;
      last = ordinal;
      const auto &s = it->second->value();
      if (hidden.contains(static_cast<unsigned>(ordinal)) ||
          s.owner != request.owner)
        continue;
      if (request.phases == PhaseSet::Terminal &&
          s.phase != ExecutionPhase::Terminal)
        continue;
      if (request.phases == PhaseSet::Nonterminal &&
          s.phase == ExecutionPhase::Terminal)
        continue;
      valid = validate_caller(*authority_, caller);
      if (!valid)
        return make_unexpected(valid.error());
      page.items.push_back({ordinal, it->second});
    }
    auto checked = validate_list_page(request, page, {200, 2000});
    if (!checked)
      return make_unexpected(checked.error());
    return page;
  }
  Result<std::unique_ptr<ObservationLease>>
  observe_changes(const CallerView &caller, const ObservationFilter &filter,
                  std::shared_ptr<ObservationReceiver> receiver) override {
    auto valid = validate_caller(*authority_, caller);
    if (!valid)
      return make_unexpected(valid.error());
    valid = validate_observation_filter(filter);
    if (!valid)
      return make_unexpected(valid.error());
    if (!receiver ||
        (filter.owner && *filter.owner != caller.description().principal))
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    for (const auto& execution : filter.executions) { auto visible = get_summary(caller, execution); if (!visible) return make_unexpected(visible.error()); }
    auto state = std::make_shared<Listener>(
        Listener{true, std::move(receiver), caller, filter});
    auto lease = std::make_unique<Lease>(state);
    listeners_.push_back(state);
    return std::unique_ptr<ObservationLease>(std::move(lease));
  }
  void emit(const SummaryInput &input) {
    auto s = ExecutionSummary::create(input);
    CHECK(s);
    for (const auto &state : listeners_) {
      if (!state->active || drop_hints ||
          !validate_caller(*authority_, state->caller))
        continue;
      bool selected =
          state->filter.owner && *state->filter.owner == input.owner;
      for (const auto &e : state->filter.executions)
        if (e == input.execution)
          selected = true;
      if (selected && input.owner == state->caller.description().principal) { auto receiver = state->receiver; if (receiver) receiver->changed({*s, ObservationTopic::Phase, false}); }
    }
  }

private:
  std::shared_ptr<Authority> authority_;
  std::vector<std::shared_ptr<Listener>> listeners_;
};
struct FactoryManifest {
  std::string version, implementation_digest;
  std::set<std::string> required;
};
inline FactoryManifest factory_manifest() {
  return {"ock.core-contracts.ports/1", OCK_TEST_FACTORY_SHA,
          {"ownership", "completion_once", "reject_cleanup",
           "exception_boundary", "drain", "worker_wait"}};
}
inline bool validate_factory_manifest(const FactoryManifest &m) {
  const auto canonical = factory_manifest();
  return m.version == canonical.version && m.implementation_digest.size() == 64 &&
         m.implementation_digest == canonical.implementation_digest &&
         m.required == canonical.required;
}
// 测试工厂的等待入口只按受管调用角色核对，不提供生产阻塞包装器。
enum class TestThreadRole { Application, Worker, Control, Domain, Database };
inline Result<void> test_wait(TestThreadRole role, bool own_sequence) {
  if (role != TestThreadRole::Application || own_sequence)
    return reject(ContractsErrc::Rejected);
  return {};
}
inline bool run_factory_contract(bool broken) {
  std::map<std::string, bool> observed;
  auto c = std::make_shared<WorkCounts>();
  if (broken) {
    auto w = std::make_unique<CountWork>(c);
    w->execute(); w->execute();
  } else {
    InlineExecutor ex;
    if (!ex.submit(std::make_unique<CountWork>(c))) return false;
  }
  observed["ownership"] = c->executed == 1 && c->destroyed == 1 && c->prepared_at_execution;
  auto receiver = std::make_shared<CountReceiver>();
  CompletionLatch latch(receiver);
  observed["completion_once"] = bool(latch.complete({})) &&
      !latch.complete({}) && receiver->count == 1 && c->completed == 1;
  auto rejected = std::make_shared<WorkCounts>();
  RejectExecutor refusal(false);
  observed["reject_cleanup"] = !refusal.submit(std::make_unique<CountWork>(rejected)) &&
      rejected->executed == 0 && rejected->destroyed == 1;
  auto fault = std::make_shared<WorkCounts>();
  InlineExecutor inline_executor;
  observed["exception_boundary"] = bool(inline_executor.submit(std::make_unique<CountWork>(fault, true))) &&
      fault->faults == 1 && fault->completed == 1 && fault->destroyed == 1;
  PhaseConditions pending{{RequiredRecordState::Recorded, 1, 0}, {}, {}, false, false};
  bool refused = !validate_phase_change(ExecutionPhase::Finalizing, ExecutionPhase::Terminal, pending);
  pending.finalization.local_work_remaining = 0;
  observed["drain"] = refused && bool(validate_phase_change(ExecutionPhase::Finalizing, ExecutionPhase::Terminal, pending));
  observed["worker_wait"] = bool(test_wait(TestThreadRole::Application, false)) &&
      !test_wait(TestThreadRole::Worker, true) && !test_wait(TestThreadRole::Control, false) &&
      !test_wait(TestThreadRole::Domain, false) && !test_wait(TestThreadRole::Database, false);
  std::set<std::string> executed;
  for (const auto &[rule, passed] : observed) { executed.insert(rule); if (!passed) return false; }
  return executed == factory_manifest().required;
}
