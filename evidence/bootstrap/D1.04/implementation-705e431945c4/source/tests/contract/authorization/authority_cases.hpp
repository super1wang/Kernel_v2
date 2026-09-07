#pragma once
#include "fixtures.hpp"

namespace policy_test::authority_cases {
struct ClaimedGrant final : CallerGrant {
  CallerDescription description_;
  explicit ClaimedGrant(CallerDescription d) : description_(std::move(d)) {}
  const CallerDescription& description() const noexcept override { return description_; }
};
struct ClaimedAuthority final : CallerAuthorityPort {
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription& d) override {
    return std::shared_ptr<const CallerGrant>(std::make_shared<ClaimedGrant>(d));
  }
  Result<void> validate(const CallerGrant&) const override { return {}; }
};
inline void authentication_source() {
  Env env;
  CHECK(env.caller->view().revalidate());
  CHECK(!env.assembly.store->open({{std::byte{8}}}, {rules(), env.auth->identity.deadline, false}));
  for (const auto& description : std::vector<CallerDescription>{
      {principal(2), {}, {}}, {principal(), principal(2), {}},
      {principal(), {}, {name("admin"), name("approved"), name("trusted")}}}) {
    CHECK(!env.session->verify(description));
    CHECK(!env.session->callers()->authenticate(description));
  }
  auto fake_grant = std::make_shared<ClaimedGrant>(env.caller->view().description());
  CHECK(!env.session->callers()->validate(*fake_grant));
  CHECK(!CallerView::check(env.session->callers(), fake_grant));
  auto forged_authority = std::make_shared<ClaimedAuthority>();
  auto forged_view = CallerView::check(forged_authority, fake_grant); CHECK(forged_view);
  CHECK(!env.session->targets()->resolve(*forged_view, target()));
  CHECK(!validate_caller(*env.session->callers(), *forged_view));
  CHECK(env.prepare());
}

inline void reconnect(Env& env, const std::vector<ScopeRule>& scope,
                       const std::vector<ScopeRule>& ceiling) {
  env.auth->identity.ceiling.rules = ceiling;
  auto session = env.assembly.store->open({{std::byte{7}}}, {scope, env.auth->identity.deadline, false});
  CHECK(session); env.session = *session;
  auto caller = env.session->verify({principal(), {}, {}});
  CHECK(caller); env.caller = *caller;
}
inline std::vector<ScopeRule>& selected(PolicyConfiguration& config,
                                       std::vector<ScopeRule>& scope, int side) {
  if (side == 0) return config.principals[0].rules;
  if (side == 1) return scope;
  if (side == 2) return config.operations[0].module_rules;
  return config.targets[0].rules;
}
inline void scope_four_way() {
  Env baseline; CHECK(baseline.prepare());
  for (int side = 0; side != 4; ++side) {
    auto config = configuration(); auto delegated = rules();
    selected(config, delegated, side).clear();
    Env denied({}, config); reconnect(denied, delegated, rules());
    CHECK(!denied.prepare()); // 新caller，不能靠旧委托世代挡住来冒充权限不足。
  }
  auto both_permissions = rules();
  for (auto& r : both_permissions) r.permissions = {name("read"), name("write")};
  for (int side = 0; side != 4; ++side) {
    auto config = configuration();
    for (auto& p : config.principals) p.rules = both_permissions;
    for (auto& o : config.operations) { o.required_permissions = {name("write")}; o.module_rules = both_permissions; }
    for (auto& t : config.targets) t.rules = both_permissions;
    auto scope = both_permissions;
    auto& split = selected(config, scope, side);
    std::erase_if(split, [](const ScopeRule& r) { return r.use == AccessUse::Invoke && r.operation == operation(); });
    split.push_back({AccessUse::Invoke, operation(), {name("read")}, {target()}, {}, {}});
    split.push_back({AccessUse::Invoke, operation(), {name("write")}, {target(2)}, {}, {}});
    Env env({}, config); reconnect(env, scope, both_permissions);
    CHECK(!env.prepare()); // (A,read)/(B,write)不能拼为(A,write)。
    auto permitted_b = env.request(); permitted_b.anchor_target = target(2); permitted_b.members[0].targets = {target(2)};
    CHECK(env.session->prepare(*env.caller, permitted_b));
  }
  for (int side = 0; side != 4; ++side) {
    auto config = configuration(); auto scope = rules();
    auto& split = selected(config, scope, side);
    std::erase_if(split, [](const ScopeRule& r) { return r.use == AccessUse::GetSummary && r.operation == operation(); });
    split.push_back({AccessUse::GetSummary, operation(), {name("allow")}, {target()}, {principal()},
        {SummaryField::Identity, SummaryField::Owner, SummaryField::Phase}});
    split.push_back({AccessUse::GetSummary, operation(), {name("allow")}, {target()}, {principal(2)},
        {SummaryField::Identity, SummaryField::Owner, SummaryField::Phase, SummaryField::Progress}});
    Env env({}, config); reconnect(env, scope, rules());
    auto input = env.source->rows[0].second.summary->value(); input.progress = {1, 2};
    env.source->rows[0].second.summary = *ExecutionSummary::create(input);
    auto result = env.session->observations()->get(*env.caller, input.execution, AccessUse::GetSummary);
    CHECK(result && result->summary->value().progress.total == 0); // 不借另一owner的Progress字段。
    Env other({}, config); reconnect(other, scope, rules());
    input.owner = principal(2);
    other.source->rows[0].second.summary = *ExecutionSummary::create(input);
    auto permitted = other.session->observations()->get(*other.caller, input.execution, AccessUse::GetSummary);
    CHECK(permitted && permitted->summary->value().progress.total == 2);
  }
  {
    auto config = configuration(); auto& split = config.principals[0].rules;
    std::erase_if(split, [](const ScopeRule& r) { return r.use == AccessUse::GetSummary && r.operation == operation(); });
    auto shape = rules()[2];
    shape.use = AccessUse::GetSummary; shape.operation = operation();
    shape.owners = {principal()}; shape.permissions = {name("not.required")}; split.push_back(shape);
    shape.owners = {principal(2)}; shape.permissions = {name("allow")}; split.push_back(shape);
    Env env({}, config);
    CHECK(!env.session->observations()->get(*env.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary));
  }
}

inline void inputs_ownership() {
  Env helpers;
  auto shared_input = std::make_shared<PolicyConfiguration>(configuration());
  auto& aliased_rules = shared_input->principals[0].rules;
  auto created = PolicyStore::create({}, std::move(*shared_input), helpers.auth, helpers.clock,
                                     helpers.digest, helpers.source);
  CHECK(created);
  aliased_rules.clear(); // const输入即使传move，外部旧元素容器不能改变已冻结政策。
  DelegationInput scope{rules(), helpers.auth->identity.deadline, false};
  auto& aliased_scope = scope.rules;
  auto session = created->store->open({{std::byte{7}}}, std::move(scope)); CHECK(session);
  aliased_scope.clear();
  helpers.auth->identity.ceiling.rules.clear(); // 返回的可信认证DTO也已拥有，不引用可变端口字段。
  auto caller = (*session)->verify({principal(), {}, {}}); CHECK(caller);
  CHECK((*session)->prepare(**caller, helpers.request()));

  auto configuration_without_owner = configuration();
  auto lifetime = std::make_shared<Lifetime>();
  configuration_without_owner.targets[0].lifetime_owner = std::shared_ptr<PortLifetime>(std::shared_ptr<PortLifetime>{}, lifetime.get());
  CHECK(configuration_without_owner.targets[0].lifetime_owner && configuration_without_owner.targets[0].lifetime_owner.use_count() == 0);
  CHECK(!PolicyStore::create({}, configuration_without_owner, helpers.auth, helpers.clock, helpers.digest, helpers.source));
  configuration_without_owner.targets[0].lifetime_owner = std::make_shared<ResourceLease>();
  CHECK(!PolicyStore::create({}, configuration_without_owner, helpers.auth, helpers.clock, helpers.digest, helpers.source));
  configuration_without_owner.targets[0].lifetime_owner = std::make_shared<ActivityLease>();
  CHECK(!PolicyStore::create({}, configuration_without_owner, helpers.auth, helpers.clock, helpers.digest, helpers.source));
  auto unowned_auth = std::shared_ptr<TrustedAuthenticationPort>(std::shared_ptr<TrustedAuthenticationPort>{}, helpers.auth.get());
  CHECK(!PolicyStore::create({}, configuration(), unowned_auth, helpers.clock, helpers.digest, helpers.source));
  auto unowned_clock = std::shared_ptr<ClockPort>(std::shared_ptr<ClockPort>{}, helpers.clock.get());
  CHECK(!PolicyStore::create({}, configuration(), helpers.auth, unowned_clock, helpers.digest, helpers.source));
  auto unowned_digest = std::shared_ptr<TrustedGroupDigestPort>(std::shared_ptr<TrustedGroupDigestPort>{}, helpers.digest.get());
  CHECK(!PolicyStore::create({}, configuration(), helpers.auth, helpers.clock, unowned_digest, helpers.source));
  auto unowned_source = std::shared_ptr<ExecutionAccessSourcePort>(std::shared_ptr<ExecutionAccessSourcePort>{}, helpers.source.get());
  CHECK(!PolicyStore::create({}, configuration(), helpers.auth, helpers.clock, helpers.digest, unowned_source));
}
} // namespace policy_test::authority_cases
