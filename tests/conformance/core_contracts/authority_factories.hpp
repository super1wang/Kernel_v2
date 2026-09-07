#pragma once
// Target增量合同的独立工厂：记录私有，描述相等不代表实际发放。
#include <thread>
namespace target_test {
class CallerIssuer final : public CallerAuthorityPort {
  class Grant final : public CallerGrant {
  public:
    explicit Grant(const CallerDescription &value) : value_(value) {}
    const CallerDescription &description() const noexcept override {
      return value_;
    }

  private:
    const CallerDescription value_;
  };
  struct Issued {
    std::shared_ptr<const Grant> grant;
    bool active = true;
  };

public:
  void enter_authenticated(PrincipalRef principal) {
    authenticated_ = principal;
  }
  Result<std::shared_ptr<const CallerGrant>>
  authenticate(const CallerDescription &request) override {
    if (request.principal != authenticated_ || request.delegated_by ||
        request.tags.size() > 8)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto grant = std::shared_ptr<const Grant>(new Grant(request));
    issued_.push_back({grant, true});
    return std::shared_ptr<const CallerGrant>(grant);
  }
  Result<void> validate(const CallerGrant &grant) const override {
    for (const auto &row : issued_)
      if (row.grant.get() == &grant && row.active)
        return {};
    return reject(ContractsErrc::InvalidGrant);
  }
  void revoke(const CallerGrant &grant) {
    for (auto &row : issued_)
      if (row.grant.get() == &grant)
        row.active = false;
  }

private:
  PrincipalRef authenticated_{};
  std::vector<Issued> issued_;
};
class TargetIssuer final : public TargetAuthorityPort {
  class View final : public TargetView {
  public:
    explicit View(foundation::ObjectId value) : value_(value) {}
    foundation::ObjectId target() const noexcept override { return value_; }

  private:
    const foundation::ObjectId value_;
  };
  struct Issued {
    std::shared_ptr<const View> grant;
    PrincipalRef caller;
    bool active;
  };

public:
  explicit TargetIssuer(std::shared_ptr<const CallerAuthorityPort> callers)
      : callers_(std::move(callers)) {}
  Result<std::shared_ptr<const TargetView>>
  resolve(const CallerView &caller, foundation::ObjectId requested) override {
    auto valid = validate_caller(*callers_, caller);
    if (!valid)
      return make_unexpected(valid.error());
    // 两个已认证主体可访问同一目标，但其grant仍分别绑定主体。
    if (requested != id<foundation::ObjectId>(9))
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto grant = std::shared_ptr<const View>(new View(requested));
    issued_.push_back({grant, caller.description().principal, true});
    return std::shared_ptr<const TargetView>(grant);
  }
  Result<void> validate(const TargetView &target, const CallerView &caller,
                        foundation::ObjectId expected) const override {
    auto valid = validate_caller(*callers_, caller);
    if (!valid)
      return valid;
    for (const auto &row : issued_)
      if (row.grant.get() == &target && row.active &&
          row.caller == caller.description().principal &&
          row.grant->target() == expected)
        return {};
    return reject(ContractsErrc::InvalidGrant);
  }
  void revoke(const TargetView &grant) {
    for (auto &row : issued_)
      if (row.grant.get() == &grant)
        row.active = false;
  }

private:
  std::shared_ptr<const CallerAuthorityPort> callers_;
  std::vector<Issued> issued_;
};
class PermitIssuer final : public PermitAuthorityPort {
  class Permit final : public ActionPermit {
  public:
    explicit Permit(const PermitBinding &binding) : binding_(binding) {}
    const PermitBinding &binding() const noexcept override { return binding_; }

  private:
    const PermitBinding binding_;
  };
  struct Issued {
    std::shared_ptr<const Permit> permit;
    bool consumed;
  };

public:
  explicit PermitIssuer(std::shared_ptr<const CallerAuthorityPort> callers)
      : callers_(std::move(callers)) {}
  Result<std::shared_ptr<const ActionPermit>>
  issue(const CallerGrant &caller, const PermitBinding &binding) override {
    auto valid = callers_->validate(caller);
    if (!valid)
      return make_unexpected(valid.error());
    if (binding.principal != caller.description().principal ||
        binding.target.empty() || !binding.permission_generation ||
        !binding.lifecycle_generation ||
        binding.deadline <= std::chrono::steady_clock::now())
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto permit = std::shared_ptr<const Permit>(new Permit(binding));
    issued_.push_back({permit, false});
    return std::shared_ptr<const ActionPermit>(permit);
  }
  Result<void> consume(const ActionPermit &permit,
                       const PermitBinding &current) override {
    ++attempts_;
    for (auto &row : issued_)
      if (row.permit.get() == &permit && !row.consumed &&
          row.permit->binding() == current &&
          current.deadline > std::chrono::steady_clock::now()) {
        row.consumed = true;
        ++consumed_;
        return {};
      }
    return reject(ContractsErrc::InvalidGrant);
  }
  unsigned attempts() const noexcept { return attempts_; }
  unsigned consumed() const noexcept { return consumed_; }

private:
  std::shared_ptr<const CallerAuthorityPort> callers_;
  std::vector<Issued> issued_;
  unsigned attempts_ = 0, consumed_ = 0;
};
// 转发同一真实grant，专门证明接收端的authority实例身份检查不可省略。
class TargetProxy final : public TargetAuthorityPort {
public:
  explicit TargetProxy(std::shared_ptr<TargetAuthorityPort> real)
      : real_(std::move(real)) {}
  Result<std::shared_ptr<const TargetView>>
  resolve(const CallerView &caller, foundation::ObjectId target) override {
    return real_->resolve(caller, target);
  }
  Result<void> validate(const TargetView &target, const CallerView &caller,
                        foundation::ObjectId expected) const override {
    return real_->validate(target, caller, expected);
  }

private:
  std::shared_ptr<TargetAuthorityPort> real_;
};
class FakePermit final : public ActionPermit {
public:
  explicit FakePermit(PermitBinding binding) : binding_(std::move(binding)) {}
  const PermitBinding &binding() const noexcept override { return binding_; }
private:
  const PermitBinding binding_;
};
class FakeTarget final : public TargetView {
public:
  foundation::ObjectId target() const noexcept override {
    return id<foundation::ObjectId>(9);
  }
};
class Records final : public EffectRecordPort {
public:
  Result<void> record_attempt(EffectId effect) override {
    if (effect.empty())
      return reject(ContractsErrc::InvalidFact);
    ++attempts;
    return {};
  }
  unsigned attempts = 0;
};
class Transitions final : public TransitionPort {
public:
  Result<Name> transition(const Name &after) override {
    ++calls;
    return after;
  }
  unsigned calls = 0;
};
struct Scenario {
  std::shared_ptr<CallerIssuer> callers = std::make_shared<CallerIssuer>();
  std::shared_ptr<TargetIssuer> targets =
      std::make_shared<TargetIssuer>(callers);
  std::shared_ptr<PermitIssuer> permits =
      std::make_shared<PermitIssuer>(callers);
  std::shared_ptr<const CallerGrant> grant_a, grant_b;
  std::optional<CallerView> a, b;
  std::shared_ptr<const TargetView> target_a, target_b;
  std::shared_ptr<const ActionPermit> permit_a, permit_b;
  PermitBinding current{{id<PrincipalId>(1)},
                        key(),
                        {},
                        id<foundation::ObjectId>(9),
                        4,
                        7,
                        std::chrono::steady_clock::now() +
                            std::chrono::seconds(60)};
  std::optional<PermitBinding> current_b;
  Name before = name("Ready");
  WorkContext work_context = work();
  Records records;
  Transitions transitions;
  Scenario() {
    CallerDescription da{{id<PrincipalId>(1)}, {}, {}},
        db{{id<PrincipalId>(2)}, {}, {}};
    callers->enter_authenticated(da.principal);
    auto ga = callers->authenticate(da);
    CHECK(ga);
    grant_a = *ga;
    callers->enter_authenticated(db.principal);
    auto gb = callers->authenticate(db);
    CHECK(gb);
    grant_b = *gb;
    a = *CallerView::check(callers, grant_a);
    b = *CallerView::check(callers, grant_b);
    target_a = *targets->resolve(*a, current.target);
    target_b = *targets->resolve(*b, current.target);
    permit_a = *permits->issue(*grant_a, current);
    current_b = current;
    current_b->principal = db.principal;
    permit_b = *permits->issue(*grant_b, *current_b);
  }
  Result<std::shared_ptr<EffectContext>> effect() {
    auto checked = EffectContext::check(callers, targets, *a, target_a, permit_a,
                                        current, work_context, records);
    if (!checked) return make_unexpected(checked.error());
    auto context = std::shared_ptr<EffectContext>(std::move(*checked));
    effects_.push_back({context, permit_a});
    return context;
  }
  Result<std::shared_ptr<TransitionView>> transition() {
    auto checked = TransitionView::check(callers, targets, *a, target_a, before,
                                         current.lifecycle_generation, permit_a,
                                         current, transitions);
    if (!checked) return make_unexpected(checked.error());
    auto view = std::shared_ptr<TransitionView>(std::move(*checked));
    transitions_.push_back({view, permit_a});
    return view;
  }
  // 协调者保存创建上下文时的原始permit及上下文owner；不按描述另挑许可。
  Result<void> accept(const EffectContext &context) {
    auto valid = context.revalidate(*callers, *targets, current);
    if (!valid) return valid;
    for (const auto &[admitted, original] : effects_)
      if (admitted.get() == &context) return permits->consume(*original, current);
    return reject(ContractsErrc::InvalidGrant);
  }
  Result<void> accept(const TransitionView &view) {
    auto valid = view.revalidate(*callers, *targets, current, before);
    if (!valid) return valid;
    for (const auto &[admitted, original] : transitions_)
      if (admitted.get() == &view) return permits->consume(*original, current);
    return reject(ContractsErrc::InvalidGrant);
  }
private:
  std::vector<std::pair<std::shared_ptr<EffectContext>,
                        std::shared_ptr<const ActionPermit>>> effects_;
  std::vector<std::pair<std::shared_ptr<TransitionView>,
                        std::shared_ptr<const ActionPermit>>> transitions_;

};
inline void context_boundary() {
  static_assert(!std::is_constructible_v<EffectContext, CallerView,
                                         std::shared_ptr<const TargetView>,
                                         std::shared_ptr<const ActionPermit>,
                                         WorkContext &, EffectRecordPort &>);
  static_assert(!std::is_move_constructible_v<EffectContext> &&
                !std::is_move_constructible_v<TransitionView>);
  Scenario s;
  auto good = s.effect();
  CHECK(good);
  CHECK((*good)->revalidate(*s.callers, *s.targets, s.current));
  CHECK(!EffectContext::check({}, s.targets, *s.a, s.target_a, s.permit_a,
                              s.current, s.work_context, s.records));
  CHECK(!EffectContext::check(s.callers, {}, *s.a, s.target_a, s.permit_a,
                              s.current, s.work_context, s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.a, {}, s.permit_a,
                              s.current, s.work_context, s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.a, s.target_a, {},
                              s.current, s.work_context, s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.a,
                              std::make_shared<FakeTarget>(), s.permit_a,
                              s.current, s.work_context, s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.a, s.target_b,
                              s.permit_a, s.current, s.work_context,
                              s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.a, s.target_a,
                              s.permit_b, s.current, s.work_context,
                              s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.b, s.target_a,
                              s.permit_b, *s.current_b, s.work_context,
                              s.records));
  CHECK(!EffectContext::check(s.callers, s.targets, *s.b, s.target_b,
                              s.permit_a, *s.current_b, s.work_context,
                              s.records));
  CHECK(s.permits->attempts() == 0 && s.permits->consumed() == 0);
  auto proxy = std::make_shared<TargetProxy>(s.targets);
  auto forwarded =
      EffectContext::check(s.callers, proxy, *s.a, s.target_a, s.permit_a,
                           s.current, s.work_context, s.records);
  CHECK(forwarded);
  CHECK(!s.accept(**forwarded));
  CHECK(s.permits->attempts() == 0);
  Scenario foreign;
  auto foreign_context = foreign.effect();
  CHECK(foreign_context);
  CHECK(!s.accept(**foreign_context));
  CHECK(s.permits->attempts() == 0);
  CHECK(!EffectContext::check(s.callers, s.targets, *foreign.a, s.target_a,
                              s.permit_a, s.current, s.work_context,
                              s.records));
  CHECK(!(*good)->revalidate(*foreign.callers, *s.targets, s.current));
  CHECK(!(*good)->revalidate(*s.callers, *foreign.targets, s.current));
  s.targets->revoke(*s.target_a);
  CHECK(!s.accept(**good));
  CHECK(s.permits->attempts() == 0);
  Scenario revoked;
  auto old = revoked.effect();
  CHECK(old);
  revoked.callers->revoke(*revoked.grant_a);
  CHECK(!revoked.accept(**old));
  CHECK(revoked.permits->attempts() == 0);
}
inline void effect_boundary() {
  {
    Scenario forged;
    const auto real = forged.permit_a;
    forged.permit_a = std::make_shared<FakePermit>(forged.current);
    auto checked = forged.effect();
    CHECK(checked); // 这里只核对绑定材料，实际发放由consume认证。
    forged.permit_a = real;
    CHECK(!forged.accept(**checked));
    CHECK(forged.permits->attempts() == 1 && forged.permits->consumed() == 0);
    auto legitimate = forged.effect();
    CHECK(legitimate && forged.accept(**legitimate));
    CHECK(forged.permits->consumed() == 1);
  }
  Scenario s;
  auto context = s.effect();
  CHECK(context);
  const auto original = s.current;
  auto reject_changed = [&](const PermitBinding &changed) {
    s.current = changed;
    CHECK(!s.accept(**context));
    CHECK(s.permits->attempts() == 0);
  };
  auto changed = original;
  changed.operation.version = ver("2.0.0");
  reject_changed(changed);
  changed = original;
  changed.group.bytes[0] = std::byte{1};
  reject_changed(changed);
  changed = original;
  changed.target = id<foundation::ObjectId>(8);
  reject_changed(changed);
  changed = original;
  ++changed.permission_generation;
  reject_changed(changed);
  changed = original;
  ++changed.lifecycle_generation;
  reject_changed(changed);
  changed = original;
  changed.principal = {id<PrincipalId>(2)};
  reject_changed(changed);
  s.current = original;
  CHECK(s.accept(**context));
  CHECK(s.permits->consumed() == 1);
  CHECK(!s.accept(**context));
  CHECK(s.permits->consumed() == 1);
  auto report = effect_handler(3, **context);
  CHECK(report.application == Application::Applied);
  Scenario expiring;
  expiring.current.deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  auto permit = expiring.permits->issue(*expiring.grant_a, expiring.current);
  CHECK(permit);
  expiring.permit_a = *permit;
  auto timed = expiring.effect();
  CHECK(timed);
  std::this_thread::sleep_until(expiring.current.deadline +
                                std::chrono::milliseconds(2));
  CHECK(!expiring.accept(**timed));
  CHECK(expiring.permits->attempts() == 0);
}
inline void lifecycle_boundary() {
  {
    Scenario forged;
    const auto real = forged.permit_a;
    forged.permit_a = std::make_shared<FakePermit>(forged.current);
    auto checked = forged.transition();
    CHECK(checked);
    forged.permit_a = real;
    CHECK(!forged.accept(**checked));
    CHECK(forged.permits->attempts() == 1 && forged.permits->consumed() == 0);
    auto legitimate = forged.transition();
    CHECK(legitimate && forged.accept(**legitimate));
    CHECK(forged.permits->consumed() == 1);
  }
  Scenario s;
  auto view = s.transition();
  CHECK(view);
  CHECK(!TransitionView::check(s.callers, s.targets, *s.a, s.target_a, s.before,
                               0, s.permit_a, s.current, s.transitions));
  CHECK(!TransitionView::check(s.callers, s.targets, *s.a, s.target_a, s.before,
                               s.current.lifecycle_generation + 1, s.permit_a,
                               s.current, s.transitions));
  CHECK(!TransitionView::check(s.callers, s.targets, *s.a, s.target_b, s.before,
                               s.current.lifecycle_generation, s.permit_a,
                               s.current, s.transitions));
  CHECK(!TransitionView::check(s.callers, s.targets, *s.a, s.target_a, s.before,
                               s.current.lifecycle_generation, s.permit_b,
                               s.current, s.transitions));
  s.before = name("Stopped");
  CHECK(!s.accept(**view));
  CHECK(s.permits->attempts() == 0);
  s.before = name("Ready");
  ++s.current.lifecycle_generation;
  CHECK(!s.accept(**view));
  CHECK(s.permits->attempts() == 0);
  --s.current.lifecycle_generation;
  Scenario foreign;
  auto foreign_view = foreign.transition();
  CHECK(foreign_view);
  CHECK(!s.accept(**foreign_view));
  CHECK(s.permits->attempts() == 0);
  auto proxy = std::make_shared<TargetProxy>(s.targets);
  auto forwarded = TransitionView::check(
      s.callers, proxy, *s.a, s.target_a, s.before,
      s.current.lifecycle_generation, s.permit_a, s.current, s.transitions);
  CHECK(forwarded);
  CHECK(!s.accept(**forwarded));
  CHECK(s.permits->attempts() == 0);
  CHECK(s.accept(**view));
  CHECK(s.permits->consumed() == 1);
  CHECK(transition_handler(3, **view).after == name("Failed"));
}
} // namespace target_test
