#pragma once
// D1.05：先写反例，直接覆盖 Policy 原生窄准入合同。
#include "tests/contract/authorization/fixtures.hpp"
#include <array>
namespace policy_inline_test {
using namespace policy_test;
inline std::shared_ptr<const DefinitionSnapshot> definition(
    std::vector<Name> permissions = {name("allow")}) {
  auto in = input(AtomicMode::PureCompute);
  in.required_permissions = std::move(permissions);
  auto made = make_compute_definition(compute_handler, in);
  CHECK(made);
  return made->snapshot();
}
inline auto targets(Env &e) {
  auto t = e.session->targets()->resolve(e.caller->view(), target());
  CHECK(t);
  return std::array<std::shared_ptr<const TargetView>, 1>{*t};
}
template<class T> void error_is(const Result<T>& r, PolicyErrc code) {
  CHECK(!r);
  CHECK(r.error().code() == policy_error(code).code());
}
struct ForgedTarget final : TargetView {
  ObjectId target() const noexcept override { return policy_test::target(); }
};
inline void ownership_and_identity() {
  Env e;
  auto d = definition();
  auto original = targets(e);
  auto auth = e.session->prepare_inline(*e.caller, d, original, 1);
  CHECK(auth);
  const std::array actual{target()};
  auto end = e.clock->now() + std::chrono::seconds(20);
  CHECK((*auth)->admit(*d, actual, {}, end));
  error_is((*auth)->admit(*definition(), actual, {}, end), PolicyErrc::ContractMismatch);
  error_is((*auth)->admit(*d, std::array{target(2)}, {}, end), PolicyErrc::InvalidInput);
  error_is((*auth)->admit(*d, std::array{target(), target()}, {}, end), PolicyErrc::InvalidInput);
  error_is(e.session->prepare_inline(*e.caller, {}, original, 1), PolicyErrc::InvalidOwner);
  std::shared_ptr<const DefinitionSnapshot> alias(std::shared_ptr<int>{}, d.get());
  error_is(e.session->prepare_inline(*e.caller, alias, original, 1), PolicyErrc::InvalidOwner);
  auto invalid = original;
  invalid[0] = std::make_shared<ForgedTarget>();
  error_is(e.session->prepare_inline(*e.caller, d, invalid, 1), PolicyErrc::InvalidAuthority);
  invalid[0] = std::shared_ptr<const TargetView>(std::shared_ptr<int>{}, original[0].get());
  error_is(e.session->prepare_inline(*e.caller, d, invalid, 1), PolicyErrc::InvalidOwner);
  auto other_session = e.assembly.store->open({{std::byte{7}}}, {rules(), end, false});
  CHECK(other_session);
  auto other_caller = (*other_session)->verify({principal(), {}, {}});
  CHECK(other_caller);
  error_is((*other_session)->prepare_inline(**other_caller, d, original, 1), PolicyErrc::InvalidAuthority);
  error_is(e.session->prepare_inline(**other_caller, d, original, 1), PolicyErrc::InvalidAuthority);
  Env foreign;
  auto foreign_targets = targets(foreign);
  error_is(e.session->prepare_inline(*e.caller, d, foreign_targets, 1), PolicyErrc::InvalidAuthority);
}
inline void invalidation() {
  for (int change = 0; change != 4; ++change) {
    Env e;
    auto d = definition();
    auto original = targets(e);
    auto auth = e.session->prepare_inline(*e.caller, d, original, 1);
    CHECK(auth);
    if (change == 0) {
      CHECK(e.assembly.administration->replace_principal_policy({principal(), {}}));
      CHECK(e.assembly.administration->replace_principal_policy({principal(), rules()}));
    } else if (change == 1) {
      CHECK(e.assembly.administration->retire_target(target()));
      CHECK(e.assembly.administration->replace_target_policy(configuration().targets[0]));
    } else if (change == 2) {
      CHECK(e.assembly.administration->set_lifecycle(target(), 2));
    } else {
      CHECK(e.session->restrict_delegation({rules(), e.clock->now() + std::chrono::seconds(30), false}));
    }
    CHECK(!(*auth)->admit(*d, std::array{target()}, {}, e.clock->now() + std::chrono::seconds(20)));
    CHECK(!e.session->prepare_inline(*e.caller, d, original, 1));
  }
}
inline void deadlines_slots_and_cancellation() {
  PolicyBudget b;
  b.inline_bindings = 1;
  b.active_inline_calls = 1;
  b.session_ttl = std::chrono::milliseconds(100);
  Env e(b);
  auto d = definition();
  auto original = targets(e);
  error_is(e.session->prepare_inline(*e.caller, d, original, 0), PolicyErrc::InvalidInput);
  error_is(e.session->prepare_inline(*e.caller, d, original, 2), PolicyErrc::BudgetExceeded);
  auto made = e.session->prepare_inline(*e.caller, d, original, 1);
  CHECK(made);
  auto auth = std::move(*made);
  error_is(e.session->prepare_inline(*e.caller, d, original, 1), PolicyErrc::BudgetExceeded);
  auto long_end = e.clock->base + std::chrono::hours(1);
  std::stop_source stop;
  stop.request_stop();
  error_is(auth->admit(*d, std::array{target()}, stop.get_token(), long_end), PolicyErrc::Cancelled);
  error_is(auth->admit(*d, std::array{target()}, {}, e.clock->base), PolicyErrc::Expired);
  {
    auto a = auth->admit(*d, std::array{target()}, {}, long_end);
    CHECK(a);
    CHECK(a->deadline() == e.clock->base + std::chrono::milliseconds(100));
    auto moved = std::move(*a);
    error_is(auth->admit(*d, std::array{target()}, {}, long_end), PolicyErrc::Busy);
    auth.reset();
    error_is(e.session->prepare_inline(*e.caller, d, original, 1), PolicyErrc::BudgetExceeded);
  }
  made = e.session->prepare_inline(*e.caller, d, original, 1);
  CHECK(made);
  auto short_end = e.clock->base + std::chrono::milliseconds(50);
  {
    auto a = (*made)->admit(*d, std::array{target()}, {}, short_end);
    CHECK(a && a->deadline() == short_end);
  }
  e.clock->elapsed = 100;
  error_is((*made)->admit(*d, std::array{target()}, {}, long_end), PolicyErrc::Expired);
}
inline void required_permissions_and_tuple() {
  for (int mismatch = 0; mismatch != 2; ++mismatch) {
    auto cfg = configuration();
    cfg.operations[0].required_permissions = mismatch ? std::vector{Name::parse("allow").value(), name("extra")} : std::vector<Name>{};
    Env e({}, cfg);
    auto original = targets(e);
    error_is(e.session->prepare_inline(*e.caller, definition(), original, 1), PolicyErrc::ContractMismatch);
  }
  // 两个权限必须由每个来源的一条完整规则覆盖，不能拼接规则。
  for (int source = 0; source != 5; ++source) {
    auto full = rules();
    for (auto &r : full) r.permissions.push_back(name("second"));
    auto split = full;
    for (auto &r : split) if (r.use == AccessUse::Invoke) r.permissions = {name("allow")};
    auto second = split;
    for (auto &r : second) if (r.use == AccessUse::Invoke) r.permissions = {name("second")};
    split.insert(split.end(), second.begin(), second.end());
    auto cfg = configuration();
    cfg.principals[0].rules = source == 0 ? split : full;
    cfg.operations[0].required_permissions = {name("allow"), name("second")};
    cfg.operations[0].module_rules = source == 3 ? split : full;
    cfg.targets[0].rules = source == 4 ? split : full;
    Env e({}, cfg);
    e.auth->identity.ceiling.rules = source == 2 ? split : full;
    auto session = e.assembly.store->open({{std::byte{7}}}, {source == 1 ? split : full, e.auth->identity.deadline, false});
    CHECK(session);
    e.session = *session;
    auto caller = e.session->verify({principal(), {}, {}});
    CHECK(caller);
    e.caller = *caller;
    auto original = targets(e);
    error_is(e.session->prepare_inline(*e.caller, definition({name("allow"), name("second")}), original, 1), PolicyErrc::Denied);
  }
}
inline void run_policy_inline_cases() {
  ownership_and_identity();
  invalidation();
  deadlines_slots_and_cancellation();
  required_permissions_and_tuple();
}
} // namespace policy_inline_test
