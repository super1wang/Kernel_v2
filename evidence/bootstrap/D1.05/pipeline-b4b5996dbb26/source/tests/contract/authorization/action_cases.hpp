#pragma once
#include "fixtures.hpp"
#include <barrier>
#include <semaphore>

namespace policy_test::action_cases {
struct Issued {
  std::shared_ptr<ActionAuthorization> action;
  std::shared_ptr<const ActionPermit> permit;
  PermitBinding expected;
};
inline Issued issue(Env& env, const ActionRequest& request) {
  auto action = env.session->prepare(*env.caller, request);
  CHECK(action);
  auto expected = (*action)->current_expected_binding();
  CHECK(expected);
  auto permit = (*action)->issue();
  CHECK(permit);
  return {*action, *permit, *expected};
}
inline Issued issue(Env& env) { return issue(env, env.request()); }
struct Counter {
  std::atomic<unsigned> attempts{0};
  bool attempt(Issued& issued) {
    if (!issued.action->consume(*issued.permit, issued.expected)) return false;
    ++attempts; // 只记录获准尝试；不是业务已应用或持久化事实。
    return true;
  }
};
struct ForgedPermit final : ActionPermit {
  PermitBinding value;
  explicit ForgedPermit(PermitBinding b) : value(std::move(b)) {}
  const PermitBinding& binding() const noexcept override { return value; }
};
struct ForgedGrant final : CallerGrant {
  CallerDescription value;
  explicit ForgedGrant(CallerDescription d) : value(std::move(d)) {}
  const CallerDescription& description() const noexcept override { return value; }
};
struct Records final : EffectRecordPort {
  unsigned calls = 0;
  Result<void> record_attempt(EffectId) override { ++calls; return {}; }
};
// 窄验证消费者保留创建context时同一个permit owner；不开放替换入口。
class CheckedEffect final {
public:
  static std::unique_ptr<CheckedEffect> create(Env& env,
      std::shared_ptr<const TargetView> target, std::shared_ptr<const ActionPermit> permit,
      const PermitBinding& expected, WorkContext& work, Records& records) {
    auto context = EffectContext::check(env.session->callers(), env.session->targets(),
        env.caller->view(), std::move(target), permit, expected, work, records);
    CHECK(context);
    return std::unique_ptr<CheckedEffect>(new CheckedEffect(std::move(*context), std::move(permit)));
  }
  Result<void> receive(Env& env, ActionAuthorization& fixed_action,
                       const PermitBinding& fixed_expected) {
    auto valid = context_->revalidate(*env.session->callers(), *env.session->targets(), fixed_expected);
    if (!valid) return valid;
    auto consumed = fixed_action.consume(*original_, fixed_expected);
    if (!consumed) return consumed;
    return context_->records().record_attempt(id<EffectId>());
  }
private:
  CheckedEffect(std::unique_ptr<EffectContext> context, std::shared_ptr<const ActionPermit> permit)
      : context_(std::move(context)), original_(std::move(permit)) {}
  std::unique_ptr<EffectContext> context_;
  std::shared_ptr<const ActionPermit> original_;
};

inline void permit_origin_binding() {
  Env env;
  const auto request = env.request();
  auto first = issue(env, request);
  auto second = issue(env, request);
  CHECK(first.expected == second.expected);
  auto forged = std::make_shared<ForgedPermit>(first.expected);
  CHECK(!first.action->consume(*forged, first.expected));
  CHECK(!first.action->consume(*second.permit, first.expected));
  auto wrong = first.expected;
  wrong.target = target(2);
  CHECK(!first.action->consume(*first.permit, wrong));
  wrong = first.expected;
  ++wrong.permission_generation;
  CHECK(!first.action->consume(*first.permit, wrong));
  wrong = first.expected;
  wrong.operation.version = ver("2.0.0");
  CHECK(!first.action->consume(*first.permit, wrong));
  ForgedGrant grant(env.caller->view().description());
  CHECK(!first.action->issue(grant, first.expected));

  // CoreContracts context只作材料配对；接收端仍用固定action及原permit身份消费。
  auto resolved = env.session->targets()->resolve(env.caller->view(), target());
  CHECK(resolved);
  WorkContext work({}, first.expected.deadline,
      *foundation::CheckedCount<std::uint64_t>::create(0, 10), name("policy.test"), {});
  Records records;
  auto swapped = CheckedEffect::create(env, *resolved, second.permit, first.expected, work, records);
  CHECK(swapped);
  CHECK(!swapped->receive(env, *first.action, first.expected));
  CHECK(records.calls == 0);
  auto original = CheckedEffect::create(env, *resolved, first.permit, first.expected, work, records);
  CHECK(original);
  CHECK(original->receive(env, *first.action, first.expected));
  CHECK(records.calls == 1);
  CHECK(!original->receive(env, *first.action, first.expected));
  CHECK(records.calls == 1);
  Counter receiver;
  CHECK(!receiver.attempt(first));
  CHECK(receiver.attempt(second));
  CHECK(receiver.attempts == 1);
}

inline void permit_once() {
  Env env;
  auto action = issue(env);
  for (unsigned n = 0; n != 5; ++n) {
    auto repeated = action.action->issue();
    CHECK(repeated && repeated->get() == action.permit.get());
  }
  Counter receiver;
  CHECK(receiver.attempt(action));
  CHECK(!receiver.attempt(action));
  CHECK(!action.action->issue());
  CHECK(receiver.attempts == 1);
}

inline void permit_concurrent() {
  Env env;
  auto action = issue(env);
  Counter receiver;
  std::barrier start(3);
  std::array<bool, 2> result{};
  std::thread a([&] { start.arrive_and_wait(); result[0] = receiver.attempt(action); });
  std::thread b([&] { start.arrive_and_wait(); result[1] = receiver.attempt(action); });
  start.arrive_and_wait();
  a.join(); b.join();
  CHECK(result[0] != result[1]);
  CHECK(receiver.attempts == 1);
  CHECK(!action.action->issue());
}

inline void revoke_before_consume() {
  Env env;
  auto action = issue(env);
  Counter receiver;
  std::binary_semaphore revoked(0);
  bool revoke_ok = false, consumed = true;
  std::thread revoke([&] {
    revoke_ok = bool(env.assembly.administration->replace_principal_policy({principal(), {}}));
    revoked.release();
  });
  std::thread consumer([&] { revoked.acquire(); consumed = receiver.attempt(action); });
  revoke.join(); consumer.join();
  CHECK(revoke_ok && !consumed && receiver.attempts == 0);
  CHECK(env.assembly.administration->replace_principal_policy({principal(), rules()}));
  CHECK(!receiver.attempt(action));
  auto fresh = issue(env);
  CHECK(receiver.attempt(fresh));
  CHECK(receiver.attempts == 1);
}

inline void consume_before_revoke() {
  Env env;
  auto action = issue(env);
  Counter receiver;
  std::binary_semaphore consumed(0);
  bool allowed = false, revoke_ok = false;
  std::thread consumer([&] { allowed = receiver.attempt(action); consumed.release(); });
  std::thread revoke([&] {
    consumed.acquire();
    revoke_ok = bool(env.assembly.administration->replace_principal_policy({principal(), {}}));
  });
  consumer.join(); revoke.join();
  CHECK(allowed && revoke_ok && receiver.attempts == 1);
  CHECK(!receiver.attempt(action));
  CHECK(!env.prepare());
  CHECK(receiver.attempts == 1);
}

inline void cancel_arbitration() {
  {
    Env env;
    auto action = issue(env);
    Counter receiver;
    std::binary_semaphore cancelled(0);
    bool cancel_ok = false, consumed = true;
    std::thread canceller([&] { cancel_ok = bool(action.action->cancel()); cancelled.release(); });
    std::thread consumer([&] { cancelled.acquire(); consumed = receiver.attempt(action); });
    canceller.join(); consumer.join();
    CHECK(cancel_ok && !consumed && receiver.attempts == 0);
    CHECK(!action.action->issue());
  }
  {
    Env env;
    auto action = issue(env);
    Counter receiver;
    std::stop_source external;
    CHECK(external.request_stop());
    CHECK(external.stop_requested());
    // 外部信号尚未交给action仲裁，不谎称取消已赢。
    CHECK(receiver.attempt(action));
    CHECK(!action.action->cancel());
    CHECK(receiver.attempts == 1);
  }
}

inline ActionRequest group_request(Env& env) {
  auto request = env.request();
  request.members.push_back({operation(2), {target(2)}});
  return request;
}
inline void group_members_complete() {
  for (int missing = 0; missing != 4; ++missing) {
    auto config = configuration();
    auto remove = [](auto& rules) {
      std::erase_if(rules, [](const ScopeRule& r) { return r.use == AccessUse::Invoke && r.operation == operation(2); });
    };
    if (missing == 0) remove(config.principals[0].rules);
    if (missing == 2) remove(config.operations[1].module_rules);
    if (missing == 3) remove(config.targets[1].rules);
    Env env({}, config);
    if (missing == 1) {
      auto delegated = rules(); remove(delegated);
      CHECK(env.session->restrict_delegation({delegated, env.auth->identity.deadline, false}));
      auto verified = env.session->verify({principal(), {}, {}});
      CHECK(verified); env.caller = *verified;
    }
    CHECK(env.prepare()); // batch/envelope本身仍可用。
    CHECK(!env.session->prepare(*env.caller, group_request(env)));
  }
  Env env;
  auto group = issue(env, group_request(env));
  Counter receiver;
  CHECK(receiver.attempt(group));
  auto changed = group_request(env);
  changed.members[1].targets.push_back(target(3));
  CHECK(!env.session->prepare(*env.caller, changed));
  changed = group_request(env);
  changed.members[1].operation.contract.bytes[0] = std::byte{1};
  CHECK(!env.session->prepare(*env.caller, changed));
}

inline ContractDigest contract_fingerprint(unsigned changed_operation) {
  auto config = configuration();
  const auto rewrite = [&](OperationSelector& selector) {
    if (changed_operation && selector == operation(changed_operation))
      selector.contract.bytes[0] = std::byte{0x45};
  };
  const auto rewrite_rules = [&](auto& rules) {
    for (auto& rule : rules) if (rule.operation) rewrite(*rule.operation);
  };
  for (auto& p : config.principals) rewrite_rules(p.rules);
  for (auto& o : config.operations) { rewrite(o.operation); rewrite_rules(o.module_rules); }
  for (auto& u : config.uses) rewrite_rules(u.module_rules);
  for (auto& t : config.targets) rewrite_rules(t.rules);
  Env env({}, config);
  auto scope = rules(); rewrite_rules(scope);
  env.auth->identity.ceiling.rules = scope;
  auto session = env.assembly.store->open({{std::byte{7}}}, {scope, env.auth->identity.deadline, false});
  CHECK(session); env.session = *session;
  auto caller = env.session->verify({principal(), {}, {}});
  CHECK(caller); env.caller = *caller;
  auto request = group_request(env);
  rewrite(request.envelope);
  for (auto& member : request.members) rewrite(member.operation);
  auto action = issue(env, request);
  return action.expected.group;
}
inline void group_substitution() {
  const auto baseline_digest = contract_fingerprint(0);
  CHECK(contract_fingerprint(1) != baseline_digest);
  CHECK(contract_fingerprint(2) != baseline_digest);
  Env env;
  env.digest->collision = true;
  const auto original_request = group_request(env);
  auto original = issue(env, original_request);
  std::array<ActionRequest, 3> changed{original_request, original_request, original_request};
  std::swap(changed[0].members[0], changed[0].members[1]);
  changed[1].members[1].targets = {target()};
  changed[2].members.pop_back();
  for (auto& request : changed) {
    auto other = issue(env, request);
    CHECK(other.expected == original.expected); // 碰撞且同锚点，字段比较不足。
    CHECK(!original.action->consume(*other.permit, original.expected));
    CHECK(!other.action->consume(*original.permit, other.expected));
    Counter own_receiver;
    CHECK(own_receiver.attempt(other));
  }
  Counter receiver;
  CHECK(receiver.attempt(original));
  CHECK(receiver.attempts == 1);
  auto later = issue(env, original_request);
  CHECK(env.assembly.administration->set_lifecycle(target(2), 2));
  CHECK(!receiver.attempt(later)); // 非anchor成员同样重验。
  CHECK(receiver.attempts == 1);
}
} // namespace policy_test::action_cases
