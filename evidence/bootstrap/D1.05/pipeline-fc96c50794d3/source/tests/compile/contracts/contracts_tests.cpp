// D1.02：33个真实注册项，禁止从expected生成发现。
#include "test_support.hpp"
#include "../../conformance/core_contracts/factories.hpp"
#include "../../conformance/core_contracts/authority_factories.hpp"
template<class T> concept PublicIssuance = requires(T &issuer) { issuer.issued; };
static_assert(!PublicIssuance<Authority>, "caller issuance must be private");
static_assert(!PublicIssuance<Publication>, "publication issuance must be private");

#include <cstdlib>
#include <cstring>
#include <type_traits>
static_assert(!std::is_constructible_v<CppTypeToken, const unsigned char *>);
static_assert(!std::is_copy_assignable_v<DefinitionSnapshot>);
static_assert(!std::is_copy_assignable_v<KnownFacts>);
static_assert(!std::is_copy_assignable_v<PreparedCommit>);
static_assert(!std::is_copy_assignable_v<ExecutionSummary>);
void type_contract_constraints() {
  static_assert(ContractValue<int> && ContractResult<void> &&
                ContractValue<std::unique_ptr<int>>);
  static_assert(!AsyncInput<Misdeclared>, "拥有声明必须为精确枚举常量");
  static_assert(!AsyncInput<MutableDeclaration>, "可变枚举不能声明异步拥有性");
  static_assert(!ContractValue<double> && !AsyncInput<Borrowed> &&
                !AsyncInput<std::string_view>);
  CHECK(make_compute_definition(void_handler, input(AtomicMode::PureCompute)));
  CHECK(make_compute_definition(move_handler, input(AtomicMode::PureCompute)));
  auto source = std::make_shared<std::string>("owned");
  Own deep{*source};
  auto d = make_compute_definition(own_handler, input(AtomicMode::PureCompute));
  CHECK(d);
  auto port = std::make_shared<Directory>(d->snapshot());
  auto b = bind<Own, Own>(port, key(), {}, Shape::Read);
  CHECK(b);
  auto frozen = prepare_submit_args(*b, std::move(deep));
  CHECK(frozen);
  source->assign("changed");
  source.reset();
  CHECK((*frozen)->text == "owned");
}
void typed_value_validation() {
  auto d = directory();
  auto b = bind<int, int>(d, key(), {}, Shape::Read);
  CHECK(b);
  int calls = 0;
  auto invoke = [&](int n) {
    auto v = validate_inline_args(*b, n);
    if (v)
      ++calls;
    return v;
  };
  CHECK(invoke(3));
  CHECK(!invoke(-1));
  CHECK(calls == 1);
  int result = -1;
  Publication pub;
  OutcomeValidation v{pub, {}, fact_budget()};
  auto rejected =
      Outcome<int>::validate(ReadCompleted<int>{result, ResultScope::ReadOnly},
                             facts(), EvidenceState::Volatile, conditions(), v);
  CHECK(!rejected);
  CHECK(calls == 1);
}
void read_shape() {
  auto d = make_read_definition(read_handler, input());
  CHECK(d);
  auto w = work();
  ReadServices<Reader> s(std::make_shared<const Reader>());
  CHECK(*read_handler(2, w, s) == 9);
  CHECK(
      make_compute_definition(compute_handler, input(AtomicMode::PureCompute)));
}
void state_edit_shape() {
  auto d =
      make_state_edit_definition(edit_handler, input(AtomicMode::StateEdit));
  CHECK(d);
  Provider::EditPort p;
  EditView<Provider> v(p, domain());
  auto w = work();
  CHECK(*edit_handler(9, v, w) == 9 && p.value == 9);
  CHECK(!validate_atomic_admission(*d, domain(2),
                                   CppTypeToken::of<OtherProvider>()));
}
void effect_shape() {
  target_test::effect_boundary();
  CHECK(make_effect_definition(effect_handler, input()));
  auto report = effect_sample();
  CHECK(report.application == Application::Applied);
}
void lifecycle_shape() {
  target_test::lifecycle_boundary();
  CHECK(make_lifecycle_definition(transition_handler, input()));
  CHECK(transition_sample().after == name("Failed"));
}
void context_capability_boundary() {
  target_test::context_boundary();
  static_assert(!std::is_copy_constructible_v<EditView<Provider>>);
  static_assert(!std::is_move_constructible_v<ReadServices<Reader>>);
  auto w = work();
  CHECK(w.charge(8));
  CHECK(!w.charge(3));
  CHECK(w.charge(2));
  CHECK(w.granted_resources().empty());
  struct Lease final : ResourceLease {};
  std::vector<std::shared_ptr<ResourceLease>> owners{std::make_shared<Lease>()};
  std::weak_ptr<ResourceLease> weak = owners.front();
  auto *retained_element = owners.data();
  WorkContext protected_work(
      {}, std::chrono::steady_clock::now(),
      *foundation::CheckedCount<std::uint64_t>::create(0, 1), name("alias"),
      std::move(owners));
  retained_element->reset();
  CHECK(!weak.expired());
  CHECK(protected_work.granted_resources().size() == 1);
}
void atomic_read_adapters() {
  auto d = make_read_definition(read_handler, input());
  CHECK(d);
  auto c =
      with_candidate_read<Provider>(*d, candidate_handler, name("provider"));
  CHECK(c);
  CHECK(validate_atomic_admission(*c, domain(), CppTypeToken::of<Provider>()));
  CHECK(!with_candidate_read<Provider>(*d, candidate_handler, name("other")));
  CHECK(!make_read_definition(read_handler, input(AtomicMode::CandidateRead)));
  auto w = work();
  Provider::CandidateReadPort r{5};
  CHECK(*candidate_handler(2, r, w) == 7);
  CHECK(!validate_atomic_domain(domain(), domain(2)));
}
void atomic_async_rejected() {
  auto in = input();
  in.execution.requires_async_dispatch = true;
  auto d = make_read_definition(read_handler, in);
  CHECK(d);
  CHECK(
      !with_candidate_read<Provider>(*d, candidate_handler, name("provider")));
  in.execution.requires_async_dispatch = false;
  in.execution.requires_external_wait = true;
  d = make_read_definition(read_handler, in);
  CHECK(d);
  CHECK(
      !with_candidate_read<Provider>(*d, candidate_handler, name("provider")));
  auto pure =
      make_compute_definition(compute_handler, input(AtomicMode::PureCompute));
  CHECK(pure);
  CHECK(
      validate_atomic_admission(*pure, domain(), CppTypeToken::of<Provider>()));
}
void operation_exact_identity() {
  CHECK(OperationVersion::parse("999999999999999999999999999.0.1", 128));
  for (auto bad :
       {"", "01.0.0", "1.0", "1.0.0.*", "latest", "1.0.-1", "1.0.0-beta"})
    CHECK(!OperationVersion::parse(bad, 128));
  CHECK(!OperationVersion::parse("1234.0.0", 7));
  CHECK(name("A") != name("a"));
  CHECK(ver("1.0.0") != ver("1.0.1"));
}
void typed_binding_fingerprint() {
  auto p = directory();
  CHECK((bind<int, int>(p, key(), {}, Shape::Read)));
  CHECK(!(bind<Other, Other>(p, key(), {}, Shape::Read)));
  CHECK(!(bind<int, int>(p, key(), {}, Shape::StateEdit)));
  auto digest = ContractDigest{};
  digest.bytes[0] = std::byte{1};
  CHECK(!(bind<int, int>(p, key(), digest, Shape::Read)));
  auto k = key();
  k.version = ver("1.0.1");
  CHECK(!(bind<int, int>(p, k, {}, Shape::Read)));
  CHECK(CppTypeToken::of<int>() != CppTypeToken::of<Other>());
}
void binding_generation_before_access() {
  auto p = directory();
  p->handle.generation = id<foundation::RegistryGeneration>(2);
  CHECK(!(bind<int, int>(p, key(), {}, Shape::Read)));
  CHECK(p->accesses == 0);
  p->handle.generation = p->gen;
  p->handle.registry = id<foundation::RegistryId>(2);
  CHECK(!(bind<int, int>(p, key(), {}, Shape::Read)));
  CHECK(p->accesses == 0);
  p->handle.registry = p->registry;
  p->handle.slot = 99;
  CHECK(!(bind<int, int>(p, key(), {}, Shape::Read)));
  CHECK(p->accesses == 0);
  p->handle.slot = 0;
  CHECK((bind<int, int>(p, key(), {}, Shape::Read)));
  CHECK(p->accesses == 1);
}
void binding_owner_lifetime() {
  auto p = directory();
  std::weak_ptr<Directory> weak = p;
  auto b = bind<int, int>(p, key(), {}, Shape::Read);
  CHECK(b);
  p.reset();
  CHECK(!weak.expired());
  CHECK(b->revalidate());
  weak.lock()->gen = id<foundation::RegistryGeneration>(2);
  auto accesses = weak.lock()->accesses;
  CHECK(!b->revalidate());
  CHECK(weak.lock()->accesses == accesses);
}
void execution_identity_separation() {
  static_assert(!std::is_convertible_v<ExecutionRef, PrincipalRef>);
  static_assert(!std::is_convertible_v<OperationHandle, foundation::TaskId>);
  auto execution = ExecutionRef{id<foundation::TaskId>()};
  auto h = id<HostIncarnation>();
  auto next = id<HostIncarnation>(2);
  CHECK(h != next);
  CHECK(execution.execution_id == id<foundation::TaskId>());
}
void outcome_closed_variants() {
  Publication pub;
  pub.publish(publication());
  auto proof = pub.attest(publication());
  CHECK(proof);
  std::vector<std::shared_ptr<const PublicationProof>> proofs{*proof};
  OutcomeValidation v{pub, proofs, fact_budget()};
  auto clean = conditions();
  CHECK(Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                               facts(), EvidenceState::Volatile, clean, v));
  CHECK(Outcome<int>::validate(
      StateCommitted<int>{id<CommitId>(), domain(), 1, 1, 2},
      facts({commit(), published()}), EvidenceState::Volatile, clean, v));
  CHECK(Outcome<int>::validate(EffectResolved<int>{effect_sample()},
                               facts({effect_fact()}), EvidenceState::Volatile,
                               clean, v));
  CHECK(Outcome<int>::validate(LifecycleResolved<int>{transition_sample()},
                               facts({lifecycle_fact()}),
                               EvidenceState::Volatile, clean, v));
  CHECK(Outcome<int>::validate(
      PlanCompleted<int>{1,
                         {{name("read"), StepStatus::Succeeded, true,
                           ChildResult::Succeeded, true}}},
      facts(), EvidenceState::Volatile, clean, v));
  auto before = clean;
  before.before_apply =
      BeforeApplyDecision{true, ApplyDecision::NotReached, true};
  CHECK(Outcome<int>::validate(
      FailedBeforeApply{name("run"), error(ContractsErrc::Rejected)}, facts(),
      EvidenceState::Volatile, before, v));
  before.before_apply->decision = ApplyDecision::CancelWon;
  CHECK(Outcome<int>::validate(
      CancelledBeforeApply{error(ContractsErrc::Rejected)}, facts(),
      EvidenceState::Volatile, before, v));
  CHECK(Outcome<int>::validate(
      PartialCompletion{{{name("effect"), StepStatus::Failed, true,
                          ChildResult::Failed, true}}},
      facts({effect_fact()}), EvidenceState::Volatile, clean, v));
  CHECK(Outcome<int>::validate(Indeterminate{{id<FactId>()}},
                               facts({unknown_fact()}),
                               EvidenceState::PersistenceUncertain, clean, v));
  CHECK(!Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                                facts(), EvidenceState::PersistenceUncertain,
                                clean, v));
  CHECK(!Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                                facts(), static_cast<EvidenceState>(99), clean,
                                v));
}
void known_facts_append_only() {
  auto old = facts({unknown_fact()});
  auto next = old->appended(
      std::vector<Fact>{
          EffectFact{id<FactId>(2), id<EffectId>(), Application::NotApplied},
          ResolutionRecord{id<FactId>(3), id<FactId>(),
                           Determination::NotApplied, "receipt"}},
      fact_budget());
  CHECK(next);
  CHECK(validate_facts_append(*old, **next));
  CHECK(!validate_facts_append(**next, *old));
  CHECK(!validate_facts_append(*old, *facts()));
  auto changed = unknown_fact();
  changed.reconcile = "changed";
  CHECK(!validate_facts_append(*old, *facts({changed})));
  CHECK(!KnownFacts::create(std::vector<Fact>{commit(), commit()},
                            fact_budget()));
  CHECK(!KnownFacts::create(std::vector<Fact>{published()}, fact_budget()));
  CHECK(!(*next)->appended(
      std::vector<Fact>{
          EffectFact{id<FactId>(4), id<EffectId>(), Application::Applied}},
      fact_budget()));
  CHECK((*next)->values().size() == 3);
}
void publication_proof_required() {
  Publication fake, real;
  fake.publish(publication());
  auto proof = fake.attest(publication());
  CHECK(proof);
  std::vector<std::shared_ptr<const PublicationProof>> ps{*proof};
  OutcomeValidation fv{fake, ps, fact_budget()}, rv{real, ps, fact_budget()};
  auto c = conditions();
  auto result = Outcome<int>::validate(
      StateCommitted<int>{id<CommitId>(), domain(), 1, 1, 1},
      facts({commit(), published()}), EvidenceState::Volatile, c, fv);
  CHECK(result);
  CHECK(!result->revalidate(rv, c, *facts()));
  CHECK(!real.validate(**proof, publication()));
  CHECK(!Outcome<int>::validate(
      StateCommitted<int>{id<CommitId>(), domain(), 1, 1, 1}, facts({commit()}),
      EvidenceState::Volatile, c, fv));
  real.publish(publication());
  auto rp = real.attest(publication());
  ps = {*rp};
  rv.proofs = ps;
  CHECK(result->revalidate(rv, c, *facts()));
}
void effect_report_preserves_fact() {
  Publication pub;
  OutcomeValidation v{pub, {}, fact_budget()};
  auto report = effect_sample();
  report.result = make_unexpected(error(ContractsErrc::InvalidContract));
  auto r = Outcome<int>::validate(EffectResolved<int>{report},
                                  facts({effect_fact()}),
                                  EvidenceState::Volatile, conditions(), v);
  CHECK(r);
  CHECK(std::holds_alternative<EffectResolved<int>>(r->value()));
  CHECK(!std::get<EffectResolved<int>>(r->value()).report.result);
  auto before = conditions();
  before.before_apply =
      BeforeApplyDecision{true, ApplyDecision::NotReached, true};
  CHECK(!Outcome<int>::validate(
      FailedBeforeApply{name("encode"), error(ContractsErrc::Rejected)},
      r->facts_owner(), EvidenceState::Volatile, before, v));
}
void lifecycle_report_preserves_state() {
  Publication pub;
  OutcomeValidation v{pub, {}, fact_budget()};
  auto report = transition_sample();
  auto r = Outcome<int>::validate(LifecycleResolved<int>{report},
                                  facts({lifecycle_fact()}),
                                  EvidenceState::Volatile, conditions(), v);
  CHECK(r);
  report.after = name("Ready");
  CHECK(!Outcome<int>::validate(LifecycleResolved<int>{report},
                                facts({lifecycle_fact()}),
                                EvidenceState::Volatile, conditions(), v));
  report = transition_sample();
  ++report.generation;
  CHECK(!Outcome<int>::validate(LifecycleResolved<int>{report},
                                facts({lifecycle_fact()}),
                                EvidenceState::Volatile, conditions(), v));
}
void before_apply_and_composite_proofs() {
  Publication pub;
  OutcomeValidation v{pub, {}, fact_budget()};
  auto forged = conditions();
  forged.before_apply =
      BeforeApplyDecision{true, ApplyDecision::CancelWon, true};
  auto r = Outcome<int>::validate(
      CancelledBeforeApply{error(ContractsErrc::Rejected)}, facts(),
      EvidenceState::Volatile, forged, v);
  CHECK(r);
  auto real = forged;
  real.before_apply->decision = ApplyDecision::NotReached;
  CHECK(!r->revalidate(v, real, *facts()));
  CHECK(!Outcome<int>::validate(
      CancelledBeforeApply{error(ContractsErrc::Rejected)}, facts(),
      EvidenceState::Volatile, real, v));
  CHECK(!Outcome<int>::validate(
      PlanCompleted<int>{1,
                         {{name("child"), StepStatus::Succeeded, true,
                           ChildResult::Unknown, false}}},
      facts({unknown_fact()}), EvidenceState::Volatile, conditions(), v));
}
void reply_phase_completion_separation() {
  SubmitReply reply =
      Accepted{{id<foundation::TaskId>()}, AcceptanceGuarantee::Volatile};
  CHECK(std::holds_alternative<Accepted>(reply));
  PhaseConditions p{{RequiredRecordState::Pending, 0, 0}, {}, {}, false, false};
  CHECK(!validate_phase_change(ExecutionPhase::Finalizing,
                               ExecutionPhase::Terminal, p));
  p.finalization.record_state = RequiredRecordState::Recorded;
  p.finalization.local_work_remaining = 1;
  CHECK(!validate_phase_change(ExecutionPhase::Finalizing,
                               ExecutionPhase::Terminal, p));
  p.finalization.local_work_remaining = 0;
  CHECK(validate_phase_change(ExecutionPhase::Finalizing,
                              ExecutionPhase::Terminal, p));
  CHECK(!validate_phase_change(ExecutionPhase::Terminal,
                               ExecutionPhase::Running, p));
  CHECK(!validate_phase_change(ExecutionPhase::Suspended,
                               ExecutionPhase::Running, p));
}
void executor_inline_ownership() {
  auto counts = std::make_shared<WorkCounts>();
  InlineExecutor ex;
  CHECK(ex.submit(std::make_unique<CountWork>(counts)));
  CHECK(counts->executed == 1 && counts->destroyed == 1 &&
        counts->completed == 1);
  CHECK(counts->prepared && counts->prepared_at_execution);
  auto early = std::make_shared<WorkCounts>(); early->prepared = false;
  CHECK(ex.submit(std::make_unique<CountWork>(early)));
  CHECK(!early->prepared_at_execution);
}
void executor_reject_exception_cleanup() {
  for (bool throwing : {false, true}) {
    auto counts = std::make_shared<WorkCounts>();
    RejectExecutor ex(throwing);
    try {
      auto r = ex.submit(std::make_unique<CountWork>(counts));
      CHECK(!r);
    } catch (const std::runtime_error &) {
      CHECK(throwing);
    }
    CHECK(counts->destroyed == 1 && counts->executed == 0 &&
          counts->completed == 0);
  }
  auto counts = std::make_shared<WorkCounts>();
  InlineExecutor ex;
  CHECK(ex.submit(std::make_unique<CountWork>(counts, true)));
  CHECK(counts->faults == 1 && counts->completed == 1 &&
        counts->destroyed == 1);
}
void completion_once_and_drain() {
  auto receiver = std::make_shared<CountReceiver>();
  CompletionLatch latch(receiver);
  CHECK(latch.complete({}));
  CHECK(!latch.complete({}));
  CHECK(receiver->count == 1);
  PhaseConditions c{
      {RequiredRecordState::Recorded, 1, 0}, {}, {}, false, false};
  CHECK(!validate_phase_change(ExecutionPhase::Finalizing,
                               ExecutionPhase::Terminal, c));
  c.finalization.local_work_remaining = 0;
  CHECK(validate_phase_change(ExecutionPhase::Finalizing,
                              ExecutionPhase::Terminal, c));
}
void atomic_prepared_identity() {
  auto identity = prepared_identity();
  std::vector<std::byte> data{std::byte{1}};
  auto p = PreparedCommit::create(identity, data, 4);
  CHECK(p);
  data[0] = std::byte{2};
  CHECK((*p)->receipt()[0] == std::byte{1});
  CHECK(validate_prepared_match(identity, (*p)->identity()));
  auto changed = identity;
  changed.base_revision++;
  CHECK(!validate_prepared_match(changed, (*p)->identity()));
  changed = identity;
  changed.reservation = id<ReservationId>(2);
  CHECK(!validate_prepared_match(changed, (*p)->identity()));
  CommitReport report{identity, CommitDisposition::DurableCommitted, {}};
  CHECK(validate_commit_report({identity, CommitDisposition::Pending}, report));
  CHECK(!validate_commit_report({identity, CommitDisposition::Published},
                                report));
  CHECK(!PreparedCommit::create(identity, data, 0));
}
void record_evidence_boundary() {
  RecordFactory factory(1);
  auto request = record_request();
  auto cap = factory.reserve(request);
  CHECK(cap);
  CHECK(!factory.reserve(request));
  auto receiver = std::make_shared<RecordCounter>();
  auto frozen = RecordRequestSnapshot::create(request, 16);
  CHECK(frozen);
  CHECK(factory.record(std::move(*cap), *frozen, receiver));
  CHECK(receiver->recorded == 1);
  CHECK(factory.available == 1);
  Publication pub;
  OutcomeValidation v{pub, {}, fact_budget()};
  auto c = conditions();
  c.finalization.record_state = RequiredRecordState::Failed;
  CHECK(!Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                                facts(), EvidenceState::RequiredRecordFailed, c,
                                v));
  c.record_failure = RecordFailure{error(ContractsErrc::Rejected), true,
                                   RepairKind::ManualReview};
  CHECK(Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                               facts(), EvidenceState::RequiredRecordFailed, c,
                               v));
  CHECK(!Outcome<int>::validate(ReadCompleted<int>{1, ResultScope::ReadOnly},
                                facts(), EvidenceState::Volatile, c, v));
}
void summary_owned_snapshot() {
  auto s = summary();
  s.facts.push_back({id<FactId>(), FactKind::Effect, Application::Applied});
  auto frozen = ExecutionSummary::create(s);
  CHECK(frozen);
  s.facts.clear();
  s.phase = ExecutionPhase::Running;
  CHECK((*frozen)->value().facts.size() == 1);
  CHECK((*frozen)->value().phase == ExecutionPhase::Finalizing);
}
void observation_host_version() {
  auto a = summary();
  auto b = a;
  b.version = *ObservationVersion::create(2);
  CHECK(*validate_summary_update(a, b, UpdateKind::Complete) ==
        UpdateDecision::Accept);
  b.host = id<HostIncarnation>(2);
  CHECK(*validate_summary_update(a, b, UpdateKind::Complete) ==
        UpdateDecision::Resync);
  b = a;
  b.phase = ExecutionPhase::Terminal;
  b.version = *ObservationVersion::create(2);
  CHECK(*validate_summary_update(a, b, UpdateKind::Complete) ==
        UpdateDecision::Accept);
  auto reopened = b;
  reopened.phase = ExecutionPhase::Running;
  reopened.version = *ObservationVersion::create(3);
  CHECK(!validate_summary_update(b, reopened, UpdateKind::Complete));
  CHECK(!ObservationVersion::create(0));
  auto max =
      ObservationVersion::create((std::numeric_limits<std::uint64_t>::max)());
  CHECK(max && !max->next());
  CHECK(*validate_summary_update(b, a, UpdateKind::Complete) ==
        UpdateDecision::Ignore);
  CHECK(*validate_summary_update(a, a, UpdateKind::Complete) ==
        UpdateDecision::Accept);
  auto conflict = a;
  conflict.progress = {1, 2, ResultScope::ReadOnly};
  CHECK(!validate_summary_update(a, conflict, UpdateKind::Complete));
  conflict = a;
  conflict.evidence = EvidenceState::Durable;
  CHECK(!validate_summary_update(a, conflict, UpdateKind::Complete));
  conflict = a;
  conflict.record_state = RequiredRecordState::Recorded;
  CHECK(!validate_summary_update(a, conflict, UpdateKind::Complete));
  conflict = a;
  conflict.fault = error(ContractsErrc::InvalidFact);
  CHECK(!validate_summary_update(a, conflict, UpdateKind::Complete));
  conflict = a;
  conflict.facts.push_back({id<FactId>(), FactKind::Effect, Application::Applied});
  CHECK(*validate_summary_update(a, conflict, UpdateKind::Complete) == UpdateDecision::Accept);
  CHECK(!validate_summary_update(conflict, a, UpdateKind::Complete));
}
void observation_budgets() {
  auto s = summary();
  s.facts.resize(9, {id<FactId>(), FactKind::Effect, Application::Applied});
  CHECK(!ExecutionSummary::create(s));
  ListRequest request{{id<PrincipalId>()}, PhaseSet::All, {0, 2000}, {}};
  ListPage page{id<HostIncarnation>(), {}, {}, name("retained")};
  CHECK(!validate_list_page(request, page, {200, 2000}));
  request.budget.page_size = 201;
  CHECK(!validate_list_page(request, page, {200, 2000}));
  request.budget.page_size = 50;
  request.budget.scan_limit = 2001;
  CHECK(!validate_list_page(request, page, {200, 2000}));
  request.budget.scan_limit = 2000;
  CHECK(validate_list_page(request, page, {200, 2000}));
}
void authorized_owner_filter() {
  auto auth = std::make_shared<Authority>();
  auto fake = std::make_shared<Authority>();
  CallerDescription desc{{id<PrincipalId>()}, {}, {}};
  auto grant = auth->authenticate(desc);
  CHECK(grant);
  auto caller = CallerView::check(auth, *grant);
  CHECK(caller);
  auto foreign = CallerView::check(fake, *fake->authenticate(desc));
  CHECK(foreign);
  ObservationFactory observer(auth);
  observer.add(summary(), 3);
  CHECK(observer.get_summary(*caller, {id<foundation::TaskId>()}));
  CHECK(!observer.get_summary(*foreign, {id<foundation::TaskId>()}));
  CHECK(!auth->validate(**fake->authenticate(desc)));
  auth->active = false;
  CHECK(!observer.get_summary(*caller, {id<foundation::TaskId>()}));
}
void completion_independent_of_observation() {
  auto auth = std::make_shared<Authority>();
  auto caller = CallerView::check(
      auth, *auth->authenticate({{id<PrincipalId>()}, {}, {}}));
  CHECK(caller);
  ObservationFactory observer(auth);
  observer.add(summary(), 1);
  auto receiver = std::make_shared<HintCounter>();
  auto lease = observer.observe_changes(
      *caller,
      ObservationFilter{
          {{id<foundation::TaskId>()}}, {}, {ObservationTopic::Phase}},
      receiver);
  CHECK(lease);
  observer.drop_hints = true;
  observer.emit(summary());
  lease->reset();
  auto completion = std::make_shared<CountReceiver>();
  CompletionLatch latch(completion);
  CHECK(latch.complete({}));
  CHECK(completion->count == 1 && receiver->count == 0);
}
void live_keyset_progress() {
  auto auth = std::make_shared<Authority>();
  auto caller = CallerView::check(
      auth, *auth->authenticate({{id<PrincipalId>()}, {}, {}}));
  CHECK(caller);
  ObservationFactory observer(auth);
  for (unsigned i = 1; i <= 4; ++i) {
    auto s = summary();
    s.execution.execution_id = id<foundation::TaskId>(i);
    observer.add(s, i);
  }
  ListRequest request{{id<PrincipalId>()}, PhaseSet::All, {1, 1}, {}};
  auto first = observer.list_summaries(*caller, request);
  CHECK(first && first->next);
  CHECK(first->items[0].listing_ordinal == 4);
  auto s = summary();
  s.execution.execution_id = id<foundation::TaskId>(5);
  observer.add(s, 5);
  request.position = first->next;
  auto second = observer.list_summaries(*caller, request);
  CHECK(second && second->items[0].listing_ordinal == 3);
  auto bad = *second;
  bad.next = request.position;
  CHECK(!validate_list_page(request, bad, {200, 2000}));
  observer.hidden.insert(2);
  request.position = second->next;
  auto empty = observer.list_summaries(*caller, request);
  CHECK(empty && empty->items.empty() && empty->next);
  CHECK(empty->next->before_ordinal < request.position->before_ordinal);
}
void port_factory_contract_manifest() {
  CHECK(run_factory_contract(false));
  CHECK(!run_factory_contract(true));
  auto manifest = factory_manifest();
  CHECK(validate_factory_manifest(manifest));
  manifest.version = "wrong";
  CHECK(!validate_factory_manifest(manifest));
  manifest = factory_manifest();
  manifest.required.erase(manifest.required.begin());
  CHECK(!validate_factory_manifest(manifest));
  manifest = factory_manifest();
  manifest.implementation_digest = "";
  CHECK(!validate_factory_manifest(manifest));
}
struct Case {
  const char *name;
  void (*run)();
};
#define CASE(T, N) {"T" #T ".contracts." #N, N}
const Case cases[] = {CASE(02, type_contract_constraints),
                      CASE(02, typed_value_validation),
                      CASE(02, read_shape),
                      CASE(02, state_edit_shape),
                      CASE(02, effect_shape),
                      CASE(02, lifecycle_shape),
                      CASE(02, context_capability_boundary),
                      CASE(02, atomic_read_adapters),
                      CASE(02, atomic_async_rejected),
                      CASE(05, operation_exact_identity),
                      CASE(05, typed_binding_fingerprint),
                      CASE(05, binding_generation_before_access),
                      CASE(05, binding_owner_lifetime),
                      CASE(05, execution_identity_separation),
                      CASE(06, outcome_closed_variants),
                      CASE(06, known_facts_append_only),
                      CASE(06, publication_proof_required),
                      CASE(06, effect_report_preserves_fact),
                      CASE(06, lifecycle_report_preserves_state),
                      CASE(06, before_apply_and_composite_proofs),
                      CASE(06, reply_phase_completion_separation),
                      CASE(06, executor_inline_ownership),
                      CASE(06, executor_reject_exception_cleanup),
                      CASE(06, completion_once_and_drain),
                      CASE(06, atomic_prepared_identity),
                      CASE(06, record_evidence_boundary),
                      CASE(19, summary_owned_snapshot),
                      CASE(19, observation_host_version),
                      CASE(19, observation_budgets),
                      CASE(19, authorized_owner_filter),
                      CASE(19, completion_independent_of_observation),
                      CASE(19, live_keyset_progress),
                      CASE(24, port_factory_contract_manifest)};
#undef CASE
class ThrowReceiver final : public CompletionPort {
public:
  Result<void> candidate_ready(Result<void>) noexcept override {
    throw std::runtime_error("receiver contract fault");
  }
};
int main(int argc, char **argv) {
  if (argc == 2 && (std::strcmp(argv[1], "--noexcept-fault") == 0 ||
                    std::strcmp(argv[1], "--noexcept-control") == 0)) {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::cerr << "contract noexcept requested\n" << std::flush;
    if (std::strcmp(argv[1], "--noexcept-fault") == 0) {
      CompletionLatch latch(std::make_shared<ThrowReceiver>());
      (void)latch.complete({});
    }
    std::cerr << "contract noexcept returned\n" << std::flush;
    return 0;
  }
  try {
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
      for (const auto &c : cases)
        std::cout << c.name << '\n';
      return 0;
    }
    if (argc == 2) {
      for (const auto &c : cases)
        if (std::strcmp(argv[1], c.name) == 0) {
          c.run();
          std::cout << "Passed " << c.name << '\n';
          return 0;
        }
    }
    return 2;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
