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
      auto recreated = configuration().targets[0];
      recreated.lifecycle_generation = 2;
      CHECK(e.assembly.administration->replace_target_policy(recreated));
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
    if (mismatch) cfg.operations[0].required_permissions.push_back(name("extra"));
    Env e({}, cfg);
    auto original = targets(e);
    auto d = mismatch ? definition() : definition({name("allow"), name("second")});
    error_is(e.session->prepare_inline(*e.caller, d, original, 1), PolicyErrc::ContractMismatch);
    // D1.04 安装入口已拒绝空权限与重复权限，不放松其既有合同。
    auto malformed = cfg.operations[0];
    malformed.required_permissions.clear();
    CHECK(!e.assembly.administration->replace_operation_policy(malformed));
    malformed.required_permissions = {name("allow"), name("allow")};
    CHECK(!e.assembly.administration->replace_operation_policy(malformed));
  }
  // 两个权限必须由每个来源的一条完整规则覆盖，不能拼接规则。
  for (int source = 0; source != 6; ++source) {
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
    auto d = definition({name("second"), name("allow")});
    auto ready = e.session->prepare_inline(*e.caller, d, original, 1);
    if (source == 5) {
      CHECK(ready);
      CHECK((*ready)->admit(*d, std::array{target()}, {}, e.auth->identity.deadline));
    } else error_is(ready, PolicyErrc::Denied);
  }
}
inline void global_quota_and_move_assignment() {
  PolicyBudget budget;
  budget.inline_bindings = 2;
  budget.active_inline_calls = 1;
  Env e(budget);
  auto d = definition();
  auto original = targets(e);
  auto first = e.session->prepare_inline(*e.caller, d, original, 1);
  auto second = e.session->prepare_inline(*e.caller, d, original, 1);
  CHECK(first && second);
  const auto end = e.auth->identity.deadline;
  {
    auto entered = (*first)->admit(*d, std::array{target()}, {}, end);
    CHECK(entered);
    error_is((*second)->admit(*d, std::array{target()}, {}, end), PolicyErrc::BudgetExceeded);
  }
  CHECK((*second)->admit(*d, std::array{target()}, {}, end));
  budget.active_inline_calls = 2;
  Env moves(budget);
  auto own = targets(moves);
  auto auth = moves.session->prepare_inline(*moves.caller, d, own, 2);
  CHECK(auth);
  auto a = (*auth)->admit(*d, std::array{target()}, {}, moves.auth->identity.deadline);
  auto b = (*auth)->admit(*d, std::array{target()}, {}, moves.auth->identity.deadline);
  CHECK(a && b);
  *a = std::move(*b);
  CHECK((*auth)->admit(*d, std::array{target()}, {}, moves.auth->identity.deadline));
  *a = std::move(*a); // 自移动不重复退还容量。
  CHECK((*auth)->admit(*d, std::array{target()}, {}, moves.auth->identity.deadline));
}
inline void close_and_original_retention() {
  for (bool close_store : {false, true}) {
    PolicyBudget budget;
    budget.inline_bindings = 1;
    Env e(budget);
    auto d = definition();
    auto own = targets(e);
    std::weak_ptr<const TargetView> retained = own[0];
    auto auth = e.session->prepare_inline(*e.caller, d, own, 1);
    CHECK(auth);
    auto admitted = (*auth)->admit(*d, std::array{target()}, {}, e.auth->identity.deadline);
    CHECK(admitted);
    auto effective = admitted->deadline();
    own[0].reset();
    if (close_store) CHECK(e.assembly.administration->close_store());
    else CHECK(e.session->close());
    error_is((*auth)->admit(*d, std::array{target()}, {}, e.auth->identity.deadline),
             close_store ? PolicyErrc::StoreClosed : PolicyErrc::SessionClosed);
    CHECK(admitted->deadline() == effective);
    auth->reset();
    CHECK(!retained.expired());
  }
}
inline void metadata_budget() {
  PolicyBudget budget;
  // 有效既有配置可以安装；巨大但合法文档不能借共享 snapshot 跳过计费。
  Env e(budget);
  auto own = targets(e);
  auto in = input(AtomicMode::PureCompute);
  in.required_permissions = {name("allow")};
  in.docs.assign(budget.text_bytes, 'x');
  in.max_docs_bytes = in.docs.size();
  auto oversized = make_compute_definition(compute_handler, in);
  CHECK(oversized);
  error_is(e.session->prepare_inline(*e.caller, oversized->snapshot(), own, 1), PolicyErrc::BudgetExceeded);
  // 失败不会消耗 binding；过大的 slot 数在实际分配前 checked 拒绝。
  auto d = definition();
  error_is(e.session->prepare_inline(*e.caller, d, own,
      (std::numeric_limits<std::size_t>::max)()), PolicyErrc::BudgetExceeded);
  CHECK(e.session->prepare_inline(*e.caller, d, own, 1));
}
inline void run_policy_inline_cases() {
  ownership_and_identity();
  invalidation();
  deadlines_slots_and_cancellation();
  required_permissions_and_tuple();
  global_quota_and_move_assignment();
  close_and_original_retention();
  metadata_budget();
}
} // namespace policy_inline_test
