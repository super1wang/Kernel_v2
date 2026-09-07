#pragma once
#include "fixtures.hpp"

namespace policy_test::session_cases {
inline std::shared_ptr<SessionAuthority> open(Env& env, const DelegationInput& scope) {
  auto session = env.assembly.store->open({{std::byte{7}}}, scope);
  CHECK(session); return *session;
}
inline std::shared_ptr<const VerifiedCaller> verify(const std::shared_ptr<SessionAuthority>& session,
                                                   PrincipalRef identity = principal()) {
  auto caller = session->verify({identity, {}, {}});
  CHECK(caller); return *caller;
}

inline void session_isolation() {
  Env env;
  auto second = open(env, {rules(), env.auth->identity.deadline, false});
  auto caller = verify(second);
  CHECK(env.caller->view().description().principal == caller->view().description().principal);
  CHECK(env.session->callers().get() != second->callers().get());
  CHECK(!env.session->prepare(*caller, env.request()));
  CHECK(!second->prepare(*env.caller, env.request()));
  CHECK(!env.session->targets()->resolve(caller->view(), target()));
  CHECK(!second->targets()->resolve(env.caller->view(), target()));
  auto a = env.session->targets()->resolve(env.caller->view(), target());
  auto b = second->targets()->resolve(caller->view(), target());
  CHECK(a && b);
  CHECK(!second->targets()->validate(**a, caller->view(), target()));
  CHECK(!env.session->targets()->validate(**b, env.caller->view(), target()));
  CHECK(second->targets()->validate(**b, caller->view(), target()));
  const auto request = env.request();
  auto action_a = env.session->prepare(*env.caller, request);
  auto action_b = second->prepare(*caller, request);
  CHECK(action_a && action_b);
  auto permit_a = (*action_a)->issue(); auto permit_b = (*action_b)->issue();
  auto binding_b = (*action_b)->current_expected_binding();
  CHECK(permit_a && permit_b && binding_b);
  CHECK(!(*action_b)->consume(**permit_a, *binding_b));
  CHECK(env.session->close());
  CHECK(!env.caller->view().revalidate());
  CHECK(caller->view().revalidate());
  CHECK((*action_b)->consume(**permit_b, *binding_b));
}

inline void service_principal() {
  Env env;
  auto old_user_action = env.prepare(); CHECK(old_user_action);
  CHECK(env.session->close());
  // 仅固定的可信认证器能认证新的独立服务会话；旧用户对象不能转换。
  env.auth->identity.principal = principal(2);
  env.auth->identity.kind = PrincipalKind::Service;
  env.auth->identity.deadline = env.clock->now() + std::chrono::seconds(2);
  auto service = open(env, {rules(), env.clock->now() + std::chrono::seconds(10), false});
  auto caller = verify(service, principal(2));
  CHECK(!env.session->verify({principal(2), {}, {}}));
  CHECK(!service->prepare(*env.caller, env.request()));
  CHECK(!(*old_user_action)->issue());
  auto action = service->prepare(*caller, env.request()); CHECK(action);
  auto binding = (*action)->current_expected_binding(); CHECK(binding);
  CHECK(binding->principal == principal(2));
  CHECK(binding->deadline == env.auth->identity.deadline);
  auto permit = (*action)->issue(); CHECK(permit);
  env.clock->elapsed = 1999;
  CHECK(caller->view().revalidate());
  env.clock->elapsed = 2000;
  CHECK(!caller->view().revalidate());
  CHECK(!(*action)->consume(**permit, *binding));
}

inline void recursive_delegation_denied() {
  Env env;
  DelegationInput scope{rules(), env.auth->identity.deadline, true};
  auto rejected = env.assembly.store->open({{std::byte{7}}}, scope);
  CHECK(!rejected && rejected.error().code() == policy_error(PolicyErrc::UnsupportedDelegation).code());
  CHECK(!env.session->restrict_delegation(scope));
  CHECK(env.prepare());
  scope.allow_redelegation = false;
  auto first = open(env, scope);
  auto caller = verify(first);
  CHECK(first->prepare(*caller, env.request()));
  env.auth->identity.ceiling.allow_redelegation = true;
  CHECK(!env.assembly.store->open({{std::byte{7}}}, scope));
  CHECK(first->prepare(*caller, env.request()));
}

inline void delegation_shrink() {
  Env env;
  auto action = env.prepare(); CHECK(action);
  auto binding = (*action)->current_expected_binding(); auto permit = (*action)->issue();
  CHECK(binding && permit);
  auto original_target = env.session->targets()->resolve(env.caller->view(), target());
  CHECK(original_target);
  auto narrowed = rules();
  for (auto& rule : narrowed) rule.targets = {target()};
  const auto deadline = env.clock->now() + std::chrono::seconds(3);
  CHECK(env.session->restrict_delegation({narrowed, deadline, false}));
  CHECK(!env.caller->view().revalidate());
  CHECK(!(*action)->consume(**permit, *binding));
  auto caller = verify(env.session);
  CHECK(!env.session->targets()->validate(**original_target, caller->view(), target()));
  auto allowed = env.session->prepare(*caller, env.request()); CHECK(allowed);
  auto limited = (*allowed)->current_expected_binding(); CHECK(limited && limited->deadline == deadline);
  auto request = env.request(); request.anchor_target = target(2); request.members[0].targets = {target(2)};
  CHECK(!env.session->prepare(*caller, request));
  CHECK(!env.session->restrict_delegation({rules(), deadline, false}));
  CHECK(!env.session->restrict_delegation({narrowed, deadline + std::chrono::milliseconds(1), false}));
  CHECK(caller->view().revalidate());
  CHECK(!(*action)->consume(**permit, *binding));
  CHECK(env.session->restrict_delegation({{}, deadline, false}));
  CHECK(!caller->view().revalidate());
  auto final_caller = verify(env.session);
  CHECK(!env.session->prepare(*final_caller, env.request()));
  CHECK(!env.session->restrict_delegation({narrowed, deadline, false}));
}

inline void operation_exact_contract() {
  {
    Env stable;
    auto action = stable.prepare(); CHECK(action);
    auto permit = (*action)->issue(); auto binding = (*action)->current_expected_binding();
    CHECK(permit && binding);
    auto response = stable.session->observations()->get(*stable.caller,
        {id<foundation::TaskId>()}, AccessUse::GetSummary); CHECK(response);
    auto rewritten = configuration().operations[0];
    rewritten.operation.contract.bytes[0] = std::byte{1};
    auto rejected = stable.assembly.administration->replace_operation_policy(rewritten);
    CHECK(!rejected && rejected.error().code() == policy_error(PolicyErrc::ContractMismatch).code());
    CHECK((*action)->consume(**permit, *binding)); // 拒绝不消耗权限generation。
    auto sink = std::make_shared<Sink>();
    auto send = SendCoordinator::create(stable.session, sink, std::make_shared<Encoder>());
    CHECK(send && (*send)->enqueue_response(response->response));
    CHECK((*send)->start_next() == StartResult::Started && sink->size == 2);
    CHECK(stable.session->observations()->get(*stable.caller,
        {id<foundation::TaskId>()}, AccessUse::GetSummary));
  }
  Env env;
  CHECK(env.prepare());
  for (unsigned mutation = 0; mutation != 3; ++mutation) {
    auto request = env.request();
    if (mutation == 0) request.envelope.operation.name = name("not.installed");
    if (mutation == 1) request.envelope.operation.version = ver("2.0.0");
    if (mutation == 2) request.envelope.contract.bytes[0] = std::byte{1};
    request.members[0].operation = request.envelope;
    CHECK(!env.session->prepare(*env.caller, request));
  }
  auto required = configuration().operations[0];
  required.required_permissions = {name("server.required")};
  CHECK(env.assembly.administration->replace_operation_policy(required));
  CHECK(!env.prepare()); // 请求不能自行宣称所需权限仍为allow。
  auto configured = configuration();
  configured.operations[0].required_permissions = {name("server.required")};
  auto add = [](auto& rules) { for (auto& rule : rules) rule.permissions.push_back(name("server.required")); };
  add(configured.principals[0].rules);
  add(configured.operations[0].module_rules);
  add(configured.targets[0].rules);
  Env exact({}, configured);
  auto delegated = rules(); add(delegated);
  exact.auth->identity.ceiling.rules = delegated;
  auto session = open(exact, {delegated, exact.auth->identity.deadline, false});
  auto caller = verify(session);
  CHECK(session->prepare(*caller, exact.request()));
  {
    Env versioned;
    auto selector = operation(); selector.operation.version = ver("2.0.0");
    auto expanded = rules();
    for (auto rule : rules()) {
      if (rule.operation == operation()) { rule.operation = selector; expanded.push_back(rule); }
    }
    OperationPolicyInput added{selector, {name("allow")}, expanded, false};
    CHECK(versioned.assembly.administration->replace_operation_policy(added));
    CHECK(versioned.assembly.administration->replace_principal_policy({principal(), expanded}));
    auto target_policy = configuration().targets[0]; target_policy.rules = expanded;
    // 此测试输入提供新owner，因此用递增世代明确重建目标实例。
    target_policy.lifecycle_generation = 2;
    CHECK(versioned.assembly.administration->replace_target_policy(target_policy));
    versioned.auth->identity.ceiling.rules = expanded;
    auto fresh_session = open(versioned, {expanded, versioned.auth->identity.deadline, false});
    auto fresh_caller = verify(fresh_session);
    auto request = versioned.request(); request.envelope = selector; request.members[0].operation = selector;
    CHECK(fresh_session->prepare(*fresh_caller, request));
  }
  {
    Env unknown;
    auto input = unknown.source->rows[0].second.summary->value(); input.operation.name = name("source.unknown");
    unknown.source->rows[0].second.summary = *ExecutionSummary::create(input);
    CHECK(!unknown.session->observations()->get(*unknown.caller, input.execution, AccessUse::GetSummary));
  }
}

inline void target_frozen_set() {
  Env env;
  auto request = env.request();
  request.members[0].targets = {target(2), target(), target(2)};
  auto action = env.session->prepare(*env.caller, request); CHECK(action);
  auto binding = (*action)->current_expected_binding(); auto permit = (*action)->issue();
  CHECK(binding && permit);
  request.members[0].targets.clear();
  request.members.clear(); request.anchor_target = target(3);
  CHECK(env.assembly.administration->set_lifecycle(target(2), 2));
  CHECK(!(*action)->consume(**permit, *binding)); // 改调用者容器不能删掉已冻结第二目标。
  auto fresh = env.request(); fresh.members[0].targets = {target(), target(2)};
  auto accepted = env.session->prepare(*env.caller, fresh); CHECK(accepted);
  fresh.members[0].targets = {target(3)}; // 后续选中状态不漂移已准备动作。
  auto current = (*accepted)->current_expected_binding(); auto p = (*accepted)->issue();
  CHECK(current && p && (*accepted)->consume(**p, *current));
  auto all = env.request(); all.members[0].targets = {target(), target(3)};
  CHECK(!env.session->prepare(*env.caller, all));
}
} // namespace policy_test::session_cases
