#include "action_cases.hpp"
#include "authority_cases.hpp"
#include "fixtures.hpp"
#include "observation_cases.hpp"
#include "send_cases.hpp"
#include "session_cases.hpp"
#include <cstdlib>
#include <new>
thread_local long allocation_failure = -1;
thread_local bool measure_allocations = false;
thread_local std::size_t largest_allocation = 0;
void *operator new(std::size_t size) {
  if (measure_allocations)
    largest_allocation = (std::max)(largest_allocation, size);
  if (allocation_failure == 0)
    throw std::bad_alloc();
  if (allocation_failure > 0)
    --allocation_failure;
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
  policy_test::authority_cases::observe_returned_buffer_free(p);
  std::free(p);
}
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
using namespace policy_test;
std::size_t selector_text(const OperationSelector &o) {
  return o.operation.name.view().size() + o.operation.version.text().size();
}
std::size_t rule_text(const std::vector<ScopeRule> &rules) {
  std::size_t n = 0;
  for (auto &r : rules) {
    if (r.operation)
      n += selector_text(*r.operation);
    for (auto &p : r.permissions)
      n += p.view().size();
  }
  return n;
}
std::size_t configured_text() {
  auto c = configuration();
  std::size_t n = 0;
  for (auto &p : c.principals)
    n += rule_text(p.rules);
  for (auto &o : c.operations) {
    n += selector_text(o.operation) + rule_text(o.module_rules);
    for (auto &p : o.required_permissions)
      n += p.view().size();
  }
  for (auto &u : c.uses) {
    n += rule_text(u.module_rules);
    for (auto &p : u.required_permissions)
      n += p.view().size();
  }
  for (auto &t : c.targets)
    n += rule_text(t.rules);
  return n + 2 * rule_text(rules());
}
void budget_contract() {
  for (std::size_t keys : {1u, 2u, 3u}) {
    PolicyBudget b;
    b.text_bytes = configured_text() + keys * selector_text(operation());
    Env e(b);
    auto response = e.session->observations()->get(
        *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(bool(response) == (keys >= 2));
    if (response) {
      auto send = SendCoordinator::create(e.session, std::make_shared<Sink>(),
                                          std::make_shared<Encoder>());
      CHECK(send);
      CHECK(bool((*send)->enqueue_response(response->response)) == (keys >= 3));
    }
  }
  for (std::size_t keys : {1u, 2u, 3u}) {
    PolicyBudget b;
    b.text_bytes = configured_text() + keys * selector_text(operation());
    Env e(b);
    ObservationFilter filter{
        {{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
    auto watch = e.session->observations()->subscribe(*e.caller, filter);
    CHECK(watch);
    auto send = SendCoordinator::create(e.session, std::make_shared<Sink>(),
                                        std::make_shared<Encoder>());
    CHECK(send);
    CHECK(bool((*send)->enqueue(**watch, {e.source->rows[0].second.summary,
                                          ObservationTopic::Progress,
                                          false})) == (keys >= 3));
    CHECK((*send)->unsubscribe(**watch));
    watch->reset();
    CHECK(e.session->observations()->subscribe(*e.caller, filter));
  }

  {
    Env e;
    e.auth->identity.kind = static_cast<PrincipalKind>(-1);
    CHECK(!e.assembly.store->open({{std::byte{7}}},
                                  {rules(), e.auth->identity.deadline, false}));
  }

  {
    Env e;
    auto oversized = configuration().principals[0];
    auto sample = oversized.rules[0];
    oversized.rules.assign(4097, sample);
    largest_allocation = 0;
    measure_allocations = true;
    auto result =
        e.assembly.administration->replace_principal_policy(oversized);
    measure_allocations = false;
    CHECK(!result);
    CHECK(largest_allocation < oversized.rules.size() * sizeof(ScopeRule));
    CHECK(e.prepare());
  }

  std::cout << "budget-session" << std::endl;
  {
    PolicyBudget b;
    b.sessions = 1;
    Env e(b);
    CHECK(e.session->close());
    CHECK(!e.assembly.store->open({{std::byte{7}}},
                                  {rules(), e.auth->identity.deadline, false}));
    std::cout << "session-denial-ok" << std::endl;
    e.caller.reset();
    std::cout << "caller-reset" << std::endl;
    e.session.reset();
    std::cout << "session-reset" << std::endl;
    CHECK(e.assembly.store->open({{std::byte{7}}},
                                 {rules(), e.auth->identity.deadline, false}));
  }
  std::cout << "budget-action" << std::endl;
  {
    PolicyBudget b;
    b.active_actions = 1;
    Env e(b);
    std::cout << "action-env" << std::endl;
    auto issued = action_cases::issue(e, e.request());
    CHECK(issued.action->consume(*issued.permit, issued.expected));
    std::cout << "action-consumed" << std::endl;
    CHECK(!e.prepare());
    std::cout << "action-denied" << std::endl;
    issued.action.reset();
    std::cout << "action-reset" << std::endl;
    CHECK(e.prepare());
    std::cout << "action-restored" << std::endl;
    auto too_many = e.request();
    const auto repeated_member = too_many.members[0];
    too_many.members.assign(b.members + 1, repeated_member);
    CHECK(!e.session->prepare(*e.caller, too_many));
  }
  std::cout << "budget-response" << std::endl;
  {
    PolicyBudget b;
    b.active_responses = 1;
    Env e(b);
    auto response = e.session->observations()->get(
        *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(response);
    auto sink = std::make_shared<Sink>();
    auto send =
        SendCoordinator::create(e.session, sink, std::make_shared<Encoder>());
    CHECK(send);
    CHECK((*send)->enqueue_response(response->response));
    CHECK((*send)->start_next() == StartResult::Started);
    CHECK(!e.session->observations()->get(*e.caller, {id<foundation::TaskId>()},
                                          AccessUse::GetSummary));
    response->response.reset();
    CHECK(e.session->observations()->get(*e.caller, {id<foundation::TaskId>()},
                                         AccessUse::GetSummary));
  }

  authority_cases::inputs_ownership();
  {
    PolicyBudget bound;
    bound.declarations = 5000;
    Env e(bound);
    std::vector<std::shared_ptr<const TargetView>> owners;
    bool denied = false;
    for (unsigned i = 0; i < 5001; ++i) {
      auto value = e.session->targets()->resolve(e.caller->view(), target());
      if (!value) {
        denied = true;
        break;
      }
      owners.push_back(*value);
    }
    CHECK(denied);
    owners.clear();
    CHECK(e.session->targets()->resolve(e.caller->view(), target()));
  }
  {
    PolicyBudget bound;
    bound.declarations = 5000;
    Env e(bound);
    std::vector<std::shared_ptr<const CallerGrant>> owners;
    bool denied = false;
    for (unsigned i = 0; i < 5001; ++i) {
      auto value = e.session->callers()->authenticate({principal(), {}, {}});
      if (!value) {
        denied = true;
        break;
      }
      owners.push_back(*value);
    }
    CHECK(denied);
    owners.clear();
    CHECK(e.session->verify({principal(), {}, {}}));
  }
  PolicyBudget tight;
  tight.text_bytes = configured_text() + 2 * selector_text(operation());
  Env probe(tight);
  CHECK(!probe.prepare());
  PolicyBudget enough;
  enough.text_bytes = configured_text() + 4 * selector_text(operation());
  Env exact(enough);
  auto action = exact.prepare();
  CHECK(action);
  auto permit = (*action)->issue();
  CHECK(permit);
  auto expected = (*action)->current_expected_binding();
  CHECK(expected);
  CHECK((*action)->consume(**permit, *expected));
  action->reset();
  auto next = exact.prepare();
  CHECK(next);
  CHECK(!(*next)->issue());
  permit->reset();
  CHECK((*next)->issue());
  PolicyBudget b;
  b.declarations = 5000;
  Env e(b);
  DelegationInput repeated{{}, e.auth->identity.deadline, false};
  repeated.rules.assign(200, rules().front());
  CHECK(!e.session->restrict_delegation(repeated));
  CHECK(e.prepare());
  repeated.rules.assign(40, rules().front());
  CHECK(e.session->restrict_delegation(repeated));
  CHECK(e.session->restrict_delegation({{}, e.auth->identity.deadline, false}));
  auto another = e.assembly.store->open(
      {{std::byte{7}}}, {rules(), e.auth->identity.deadline, false});
  CHECK(another);
}
void clock_boundaries() {
  {
    Env e;
    auto request = e.request();
    request.requested_deadline = e.clock->now();
    CHECK(!e.session->prepare(*e.caller, request));
    request.requested_deadline = e.clock->now() + std::chrono::minutes(1);
    auto prepared = e.session->prepare(*e.caller, request);
    CHECK(prepared);
    auto expected = (*prepared)->current_expected_binding();
    CHECK(expected &&
          expected->deadline == e.clock->now() + std::chrono::seconds(30));
    auto permit = (*prepared)->issue();
    CHECK(permit);
    e.clock->elapsed = 30000;
    CHECK(!(*prepared)->consume(**permit, *expected));
  }

  auto clock = std::make_shared<Clock>();
  clock->base = TimePoint(TimePoint::duration(-1));
  auto auth = std::make_shared<Auth>(clock->now() + std::chrono::hours(1));
  auto created = PolicyStore::create({}, configuration(), auth, clock,
                                     std::make_shared<Digest>(),
                                     std::make_shared<Source>());
  CHECK(created);
  clock->base = TimePoint::max() - std::chrono::seconds(1);
  CHECK(!PolicyStore::create({}, configuration(), auth, clock,
                             std::make_shared<Digest>(),
                             std::make_shared<Source>()));
  clock->base = TimePoint{};
  PolicyBudget b;
  b.session_ttl = std::chrono::milliseconds::max();
  CHECK(!PolicyStore::create(b, configuration(), auth, clock,
                             std::make_shared<Digest>(),
                             std::make_shared<Source>()));
}
void target_issued_identity() {
  struct ClaimedTarget final : TargetView {
    ObjectId target() const noexcept override { return policy_test::target(); }
  };
  Env target_control;
  ClaimedTarget forged;
  CHECK(!target_control.session->targets()->validate(
      forged, target_control.caller->view(), target()));

  auto cfg = configuration();
  cfg.targets[1].rules.clear();
  Env e({}, cfg);
  auto a = e.session->targets()->resolve(e.caller->view(), target());
  CHECK(a);
  auto b = e.session->targets()->resolve(e.caller->view(), target(2));
  auto c = e.session->targets()->resolve(e.caller->view(), target(3));
  CHECK(!c);
  auto read = configuration();
  for (auto &r : read.principals)
    std::erase_if(r.rules,
                  [](auto &x) { return x.use != AccessUse::ReadResult; });
  Env reader({}, read);
  CHECK(reader.session->targets()->resolve(reader.caller->view(), target()));
  auto crossed = configuration();
  std::erase_if(crossed.principals[0].rules,
                [](auto &r) { return r.use != AccessUse::ReadResult; });
  std::erase_if(crossed.targets[0].rules,
                [](auto &r) { return r.use != AccessUse::GetSummary; });
  Env mismatch({}, crossed);
  auto denied =
      mismatch.session->targets()->resolve(mismatch.caller->view(), target());
  CHECK(!denied);
  CHECK(!b);
  CHECK(b.error().code() == c.error().code());
}
void page_owner_authorization() {
  Env e;
  ListRequest request{principal(), PhaseSet::All, {1, 1}, {}};
  auto page = e.session->observations()->list(*e.caller, request, {});
  CHECK(page);
  CHECK(page->page.items.size() == 1);
  CHECK(page->continuation);
  auto next =
      e.session->observations()->list(*e.caller, request, page->continuation);
  CHECK(next && next->page.items.size() == 1);
  CHECK(next->page.items[0].listing_ordinal <
        page->page.items[0].listing_ordinal);
  auto denied = request;
  denied.owner = principal(3);
  auto before = e.source->scans;
  CHECK(!e.session->observations()->list(*e.caller, denied, {}));
  CHECK(e.source->scans == before);
}
void subscription_scope_atomic() {
  {
    Env e;
    ObservationFilter bad{
        {{id<foundation::TaskId>()}}, {}, {static_cast<ObservationTopic>(-1)}};
    CHECK(!e.session->observations()->subscribe(*e.caller, bad));
  }

  {
    PolicyBudget b;
    b.active_watches = 1;
    Env e(b);
    ObservationFilter filter{
        {{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
    auto watch = e.session->observations()->subscribe(*e.caller, filter);
    CHECK(watch);
    watch->reset();
    e.auth->identity.principal = principal(2);
    auto second = e.assembly.store->open(
        {{std::byte{7}}}, {rules(), e.auth->identity.deadline, false});
    CHECK(second);
    auto caller = (*second)->verify({principal(2), {}, {}});
    CHECK(caller);
    CHECK((*second)->observations()->subscribe(**caller, filter));
  }

  {
    PolicyBudget b;
    b.active_watches = 1;
    b.watches_per_session = 1;
    b.watches_per_principal = 1;
    Env atomic(b);
    ObservationFilter invalid{
        {{id<foundation::TaskId>()}, {id<foundation::TaskId>(3)}},
        {},
        {ObservationTopic::Progress}};
    CHECK(!atomic.session->observations()->subscribe(*atomic.caller, invalid));
    invalid.executions.pop_back();
    auto valid =
        atomic.session->observations()->subscribe(*atomic.caller, invalid);
    CHECK(valid);
    CHECK(!atomic.session->observations()->subscribe(*atomic.caller, invalid));
    valid->reset();
    CHECK(atomic.session->observations()->subscribe(*atomic.caller, invalid));
  }
  Env e;
  ObservationFilter f{
      {{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
  auto watch = e.session->observations()->subscribe(*e.caller, f);
  CHECK(watch);
  auto bad = f;
  bad.executions.push_back({id<foundation::TaskId>(3)});
  CHECK(!e.session->observations()->subscribe(*e.caller, bad));
  auto sink = std::make_shared<Sink>();
  auto send =
      SendCoordinator::create(e.session, sink, std::make_shared<Encoder>());
  CHECK(send);
  CHECK((*send)->enqueue(**watch, {e.source->rows[0].second.summary,
                                   ObservationTopic::Progress, false}));
  CHECK((*send)->start_next() == StartResult::Started);
  CHECK(sink->size == 2);
  auto removed = (*send)->unsubscribe(**watch);
  CHECK(removed && *removed);
  CHECK(!(*send)->enqueue(**watch, {e.source->rows[0].second.summary,
                                    ObservationTopic::Progress, false}));
}

void owner_scope() {
  Env allowed;
  auto input = allowed.source->rows[0].second.summary->value();
  input.owner = principal(2);
  allowed.source->rows[0].second.summary = *ExecutionSummary::create(input);
  CHECK(allowed.session->observations()->get(*allowed.caller, input.execution,
                                             AccessUse::GetSummary));
  auto cfg = configuration();
  for (auto &r : cfg.principals[0].rules)
    r.owners = {principal()};
  Env denied({}, cfg);
  denied.source->rows[0].second.summary = *ExecutionSummary::create(input);
  CHECK(!denied.session->observations()->get(*denied.caller, input.execution,
                                             AccessUse::GetSummary));
}
void target_lifecycle() {
  Env e;
  auto view = e.session->targets()->resolve(e.caller->view(), target());
  CHECK(view);
  auto issued = action_cases::issue(e, e.request());
  CHECK(e.assembly.administration->set_lifecycle(target(), 2));
  CHECK(!e.session->targets()->validate(**view, e.caller->view(), target()));
  CHECK(!issued.action->consume(*issued.permit, issued.expected));
  CHECK(!e.assembly.administration->set_lifecycle(target(), 2));
  CHECK(e.assembly.administration->retire_target(target()));
  auto replacement = configuration().targets[0];
  replacement.lifecycle_generation = 3;
  CHECK(e.assembly.administration->replace_target_policy(replacement));
  auto fresh = e.session->targets()->resolve(e.caller->view(), target());
  CHECK(fresh);
  CHECK(!e.session->targets()->validate(**view, e.caller->view(), target()));
}
void generation_exhaustion() {
  PolicyBudget b;
  b.generation_limit = 2;
  Env e(b);
  CHECK(e.assembly.administration->replace_principal_policy(
      configuration().principals[0]));
  CHECK(!e.assembly.administration->replace_principal_policy(
      configuration().principals[0]));
  CHECK(!e.assembly.administration->replace_principal_policy(
      configuration().principals[0]));
  CHECK(e.prepare());
  PolicyBudget ids;
  ids.identity_limit = 4;
  StoreId first{};
  for (int i = 0; i < 2; ++i) {
    Env limited(ids);
    auto response = limited.session->observations()->get(
        *limited.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(response);
    CHECK(!limited.session->observations()->get(
        *limited.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary));
    auto sink = std::make_shared<Sink>();
    auto send = SendCoordinator::create(limited.session, sink,
                                        std::make_shared<Encoder>());
    CHECK(send);
    CHECK((*send)->enqueue_response(response->response));
    CHECK((*send)->start_next() == StartResult::Started);
    if (i == 0)
      first = sink->last_store;
    else
      CHECK(first != sink->last_store);
    CHECK(limited.assembly.administration->close_store());
  }
}
void exception_atomicity() {
  {
    PolicyBudget b;
    b.sessions = 2;
    b.active_watches = 1;
    b.watches_per_session = 1;
    b.watches_per_principal = 1;
    Env e(b);
    e.auth->throw_allocation = true;
    CHECK(!e.assembly.store->open({{std::byte{7}}},
                                  {rules(), e.auth->identity.deadline, false}));
    e.auth->throw_allocation = false;
    auto second = e.assembly.store->open(
        {{std::byte{7}}}, {rules(), e.auth->identity.deadline, false});
    CHECK(second);
    ObservationFilter filter{
        {{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
    e.source->find_failure = true;
    CHECK(!e.session->observations()->subscribe(*e.caller, filter));
    e.source->find_failure = false;
    auto watch = e.session->observations()->subscribe(*e.caller, filter);
    CHECK(watch);
    CHECK(!e.session->observations()->subscribe(*e.caller, filter));
    watch->reset();
    CHECK(e.session->observations()->subscribe(*e.caller, filter));
  }

  {
    Env e;
    e.digest->throw_allocation = true;
    CHECK(!e.prepare());
    e.digest->throw_allocation = false;
    CHECK(e.prepare());
    e.source->find_failure = true;
    CHECK(!e.session->observations()->get(*e.caller, {id<foundation::TaskId>()},
                                          AccessUse::GetSummary));
    e.source->find_failure = false;
    auto response = e.session->observations()->get(
        *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(response);
    e.source->scan_failure = true;
    CHECK(!e.session->observations()->list(
        *e.caller, {principal(), PhaseSet::All, {1, 1}, {}}, {}));
    e.source->scan_failure = false;
    CHECK(e.session->observations()->list(
        *e.caller, {principal(), PhaseSet::All, {1, 1}, {}}, {}));
    auto sink = std::make_shared<Sink>();
    auto encoder = std::make_shared<Encoder>();
    auto send = SendCoordinator::create(e.session, sink, encoder);
    CHECK(send);
    encoder->throw_allocation = true;
    CHECK(!(*send)->enqueue_response(response->response));
    encoder->throw_allocation = false;
    sink->throw_allocation = true;
    CHECK(!(*send)->enqueue_response(response->response));
    sink->throw_allocation = false;
    CHECK((*send)->enqueue_response(response->response));
    e.source->find_failure = true;
    auto failed = (*send)->start_next();
    CHECK(!failed);
    CHECK(sink->size == 0);
    e.source->find_failure = false;
    CHECK((*send)->start_next() == StartResult::Started);
  }
  for (int failure = 0; failure < 20; ++failure) {
    std::cout << "allocation-point " << failure << std::endl;
    Env e;
    std::cout << "env-ready" << std::endl;
    auto issued = action_cases::issue(e, e.request());
    std::cout << "issued" << std::endl;
    PrincipalPolicyInput revoke{principal(), {}};
    allocation_failure = failure;
    auto update = e.assembly.administration->replace_principal_policy(revoke);
    allocation_failure = -1;
    std::cout << "update-returned" << std::endl;
    auto consumed = issued.action->consume(*issued.permit, issued.expected);
    CHECK(bool(consumed) == !bool(update));
    std::cout << "consume-returned" << std::endl;
  }
  std::cout << "owner-destruction" << std::endl;
  struct Reentrant final : PortLifetime {
    std::function<void()> destroy;
    ~Reentrant() {
      if (destroy)
        destroy();
    }
  };
  auto cfg = configuration();
  auto owner = std::make_shared<Reentrant>();
  cfg.targets[0].lifetime_owner = owner;
  Env e({}, std::move(cfg));
  bool destroyed = false;
  owner->destroy = [&] {
    std::cout << "owner-callback-enter" << std::endl;
    destroyed = true;
    CHECK(e.assembly.administration->close_store());
  };
  owner.reset();
  auto replacement = configuration().targets[0];
  replacement.lifecycle_generation = 2;
  CHECK(e.assembly.administration->replace_target_policy(replacement));
  CHECK(destroyed);
}
void query_field_projection() {
  observation_cases::noninvoke_permissions();
  {
    auto cfg = configuration();
    for (auto &r : cfg.principals[0].rules)
      if (r.use == AccessUse::ReadResult)
        r.permissions.clear();
    Env uses({}, cfg);
    CHECK(uses.session->observations()->get(
        *uses.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary));
    CHECK(!uses.session->observations()->get(
        *uses.caller, {id<foundation::TaskId>()}, AccessUse::ReadResult));
  }
  {
    auto cfg = configuration();
    for (auto &r : cfg.principals[0].rules)
      if (r.use == AccessUse::GetSummary)
        r.permissions.clear();
    Env cancel({}, cfg);
    CHECK(!cancel.session->observations()->get(*cancel.caller,
                                               {id<foundation::TaskId>()},
                                               AccessUse::CancelExecution));
  }
  auto cfg = configuration();
  for (auto &r : cfg.principals[0].rules)
    if (r.use == AccessUse::GetSummary)
      r.fields = {SummaryField::Identity, SummaryField::Owner,
                  SummaryField::Phase};
  Env e({}, cfg);
  auto input = e.source->rows[0].second.summary->value();
  input.progress = {1, 2};
  input.parent = ExecutionRef{id<foundation::TaskId>(3)};
  e.source->rows[0].second.summary = *ExecutionSummary::create(input);
  auto value = e.session->observations()->get(*e.caller, input.execution,
                                              AccessUse::GetSummary);
  CHECK(value);
  CHECK(value->summary->value().progress.total == 0);
  CHECK(!value->summary->value().parent);
  for (auto &r : cfg.principals[0].rules)
    if (r.use == AccessUse::GetSummary)
      r.fields = {SummaryField::Identity, SummaryField::Phase};
  CHECK(e.assembly.administration->replace_principal_policy(cfg.principals[0]));
  CHECK(!e.session->observations()->get(*e.caller, input.execution,
                                        AccessUse::GetSummary));
}
void page_binding_isolation() {
  observation_cases::page_binding_dimensions();
  Env e;
  ListRequest q{principal(), PhaseSet::All, {1, 1}, {}};
  auto page = e.session->observations()->list(*e.caller, q, {});
  CHECK(page && page->continuation);
  auto other = e.assembly.store->open(
      {{std::byte{7}}}, {rules(), e.auth->identity.deadline, false});
  CHECK(other);
  auto caller = (*other)->verify({principal(), {}, {}});
  CHECK(caller);
  CHECK(!(*other)->observations()->list(**caller, q, page->continuation));
  auto changed = q;
  changed.budget.page_size = 2;
  CHECK(
      !e.session->observations()->list(*e.caller, changed, page->continuation));
  e.clock->elapsed = 300000;
  CHECK(!e.session->observations()->list(*e.caller, q, page->continuation));
}
void page_revoke_live_keyset() {
  {
    auto cfg = configuration();
    cfg.targets[0].rules.clear();
    Env hidden({}, cfg);
    ListRequest query{principal(), PhaseSet::All, {1, 1}, {}};
    auto empty =
        hidden.session->observations()->list(*hidden.caller, query, {});
    CHECK(empty && empty->page.items.empty() && empty->continuation);
    auto visible = hidden.session->observations()->list(*hidden.caller, query,
                                                        empty->continuation);
    CHECK(visible && visible->page.items.size() == 1 &&
          visible->page.items[0].listing_ordinal == 1);
  }
  Env e;
  ListRequest q{principal(), PhaseSet::All, {1, 1}, {}};
  auto page = e.session->observations()->list(*e.caller, q, {});
  CHECK(page && page->continuation);
  auto row = e.source->rows.front();
  row.first = 3;
  e.source->rows.insert(e.source->rows.begin(), row);
  auto next = e.session->observations()->list(*e.caller, q, page->continuation);
  CHECK(next && next->page.items.size() == 1 &&
        next->page.items[0].listing_ordinal == 1);
  CHECK(e.assembly.administration->replace_principal_policy(
      configuration().principals[0]));
  CHECK(!e.session->observations()->list(*e.caller, q, page->continuation));
}
void internal_component_boundary() {
  static_assert(!std::is_copy_constructible_v<PolicyStore>);
  static_assert(!std::is_constructible_v<ActionAuthorization, PermitBinding>);
  Env e;
  CHECK(e.prepare());
  std::cout << "internal_policy_consumer=active; SDK Runtime boundary is "
               "verified by paired child wrapper\n";
}
void queued_revoke_drop() {
  observation_cases::source_store_lifetime();
  {
    Env e;
    auto input = e.source->rows[0].second.summary->value();
    input.host = id<HostIncarnation>(2);
    e.source->rows[0].second.summary = *ExecutionSummary::create(input);
    CHECK(!e.session->observations()->get(*e.caller, input.execution,
                                          AccessUse::GetSummary));
    CHECK(!e.prepare());
  }

  for (int mode = 0; mode < 5; ++mode) {
    Env e;
    auto old = e.prepare();
    CHECK(old);
    auto response = e.session->observations()->get(
        *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(response);
    auto sink = std::make_shared<Sink>();
    auto send =
        SendCoordinator::create(e.session, sink, std::make_shared<Encoder>());
    CHECK(send);
    CHECK((*send)->enqueue_response(response->response));
    if (mode == 0) {
      CHECK(e.assembly.administration->close_store());
      e.source->source_id.host = id<HostIncarnation>(2);
    }
    if (mode == 1)
      e.source->source_id.host = id<HostIncarnation>(2);
    if (mode == 2) {
      auto raw = e.source->rows[0].second.summary->value();
      raw.owner = principal(2);
      e.source->rows[0].second.summary = *ExecutionSummary::create(raw);
    }
    if (mode == 3)
      CHECK(e.assembly.administration->replace_principal_policy(
          {principal(), {}}));
    if (mode == 4) {
      auto raw = e.source->rows[0].second.summary->value();
      raw.progress = {1, 2};
      raw.version = *raw.version.next();
      e.source->rows[0].second.summary = *ExecutionSummary::create(raw);
      CHECK((*send)->start_next() == StartResult::Started);
      CHECK(sink->size == 2);
      continue;
    }
    CHECK(!(*send)->start_next());
    CHECK(sink->size == 0);
    if (mode < 3)
      CHECK(!e.prepare());
  }
  {
    Env page;
    auto response = page.session->observations()->list(
        *page.caller, {principal(), PhaseSet::All, {1, 1}, {}}, {});
    CHECK(response);
    auto sink = std::make_shared<Sink>();
    auto send = SendCoordinator::create(page.session, sink,
                                        std::make_shared<Encoder>());
    CHECK(send);
    CHECK((*send)->enqueue_response(response->response));
    CHECK(page.assembly.administration->replace_principal_policy(
        {principal(), {}}));
    CHECK(!(*send)->start_next());
    CHECK(sink->size == 0);
  }
  {
    Env e;
    e.source->source_id.restore = RestoreMode::Present;
    CHECK(!PolicyStore::create({}, configuration(), e.auth, e.clock, e.digest,
                               e.source));
    e.source->source_id.restore = static_cast<RestoreMode>(99);
    CHECK(!PolicyStore::create({}, configuration(), e.auth, e.clock, e.digest,
                               e.source));
  }
  Env e;
  auto response = e.session->observations()->get(
      *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
  CHECK(response);
  auto sink = std::make_shared<Sink>();
  auto send =
      SendCoordinator::create(e.session, sink, std::make_shared<Encoder>());
  CHECK(send);
  CHECK((*send)->enqueue_response(response->response));
  CHECK(e.assembly.administration->close_store());
  CHECK(!(*send)->start_next());
  CHECK(sink->size == 0);
}
int main(int argc, char **argv) {
  try {
    CHECK(argc == 2);
    const std::map<std::string, void (*)()> cases{
        {"T20.policy.transmission_start_arbitration",
         send_cases::transmission_start_arbitration},
        {"T20.policy.failed_start_not_started",
         send_cases::failed_start_not_started},
        {"T20.policy.unknown_start_no_retry",
         send_cases::unknown_start_no_retry},
        {"T20.policy.unsubscribe_inflight", send_cases::unsubscribe_inflight},
        {"T20.policy.coordinator_budget", send_cases::coordinator_budget},
        {"T20.policy.control_queue_reserve", send_cases::control_queue_reserve},
        {"T19.policy.subscription_connection_cleanup",
         send_cases::subscription_connection_cleanup},
        {"T07.policy.owner_scope", owner_scope},
        {"T07.policy.target_lifecycle", target_lifecycle},
        {"T07.policy.generation_exhaustion", generation_exhaustion},
        {"T07.policy.exception_atomicity", exception_atomicity},
        {"T19.policy.query_field_projection", query_field_projection},
        {"T19.policy.page_binding_isolation", page_binding_isolation},
        {"T19.policy.page_revoke_live_keyset", page_revoke_live_keyset},
        {"T20.policy.internal_component_boundary", internal_component_boundary},
        {"T07.policy.session_isolation", session_cases::session_isolation},
        {"T07.policy.service_principal", session_cases::service_principal},
        {"T07.policy.recursive_delegation_denied",
         session_cases::recursive_delegation_denied},
        {"T07.policy.delegation_shrink", session_cases::delegation_shrink},
        {"T07.policy.operation_exact_contract",
         session_cases::operation_exact_contract},
        {"T07.policy.target_frozen_set", session_cases::target_frozen_set},
        {"T07.policy.owned_inputs_budget", budget_contract},
        {"T07.policy.authentication_source",
         authority_cases::authentication_source},
        {"T07.policy.expiry_boundary", clock_boundaries},
        {"T07.policy.scope_four_way", authority_cases::scope_four_way},
        {"T07.policy.target_issued_identity", target_issued_identity},
        {"T07.policy.permit_once", action_cases::permit_once},
        {"T07.policy.revoke_before_consume",
         action_cases::revoke_before_consume},
        {"T07.policy.cancel_arbitration", action_cases::cancel_arbitration},
        {"T07.policy.permit_origin_binding",
         action_cases::permit_origin_binding},
        {"T07.policy.permit_concurrent", action_cases::permit_concurrent},
        {"T07.policy.consume_before_revoke",
         action_cases::consume_before_revoke},
        {"T07.policy.group_members_complete",
         action_cases::group_members_complete},
        {"T07.policy.group_substitution", action_cases::group_substitution},
        {"T19.policy.page_owner_authorization", page_owner_authorization},
        {"T19.policy.subscription_scope_atomic", subscription_scope_atomic},
        {"T20.policy.queued_revoke_drop", queued_revoke_drop}};
    if (std::string(argv[1]) == "--list") {
      for (auto &[n, f] : cases)
        std::cout << n << '\n';
      return 0;
    }
    auto found = cases.find(argv[1]);
    CHECK(found != cases.end());
    found->second();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
