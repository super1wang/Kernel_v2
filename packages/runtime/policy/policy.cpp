#include "policy.hpp"
#include "validation.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
namespace ock::runtime::policy {
namespace detail {
template <class T> bool has(const std::vector<T> &v, const T &x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}
template <class T> bool unique(const std::vector<T> &v) {
  for (std::size_t i = 0; i < v.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (v[i] == v[j])
        return false;
  return true;
}
template <class T>
bool subset(const std::vector<T> &a, const std::vector<T> &b) {
  for (auto &x : a)
    if (!has(b, x))
      return false;
  return true;
}
template <class T> bool owned(const std::shared_ptr<T> &p) {
  return p && p.use_count() > 0;
}
template <class T> Result<T> failure(PolicyErrc e) {
  return make_unexpected(policy_error(e));
}
struct Usage {
  std::size_t declarations = 0, text = 0;
};
bool charge(std::size_t &used, std::size_t extra, std::size_t limit) {
  if (extra > limit - used)
    return false;
  used += extra;
  return true;
}
struct Meter {
  const PolicyBudget &b;
  Usage value;
  bool valid = true;
  void count(std::size_t n) {
    valid = charge(value.declarations, n, b.declarations) && valid;
  }
  void text(std::string_view s) {
    valid = charge(value.text, s.size(), b.text_bytes) && valid;
  }
  void selector(const OperationSelector &o) {
    text(o.operation.name.view());
    text(o.operation.version.text());
  }
  void rules(const std::vector<ScopeRule> &v) {
    count(v.size());
    if (v.size() > b.rules) {
      valid = false;
      return;
    }
    for (auto &r : v) {
      if (r.use < AccessUse::Invoke || r.use > AccessUse::Subscribe ||
          (r.use == AccessUse::Invoke && !r.operation))
        valid = false;
      if (r.operation)
        selector(*r.operation);
      count(r.permissions.size());
      count(r.targets.size());
      count(r.owners.size());
      count(r.fields.size());
      for (auto &n : r.permissions)
        text(n.view());
      if (!unique(r.permissions) || !unique(r.targets) || !unique(r.owners) ||
          !unique(r.fields))
        valid = false;
      for (auto t : r.targets)
        if (t.empty())
          valid = false;
      for (auto o : r.owners)
        if (o.principal_id.empty())
          valid = false;
      for (auto f : r.fields)
        if (f < SummaryField::Identity || f > SummaryField::Facts)
          valid = false;
    }
  }
};
bool valid_budget(const PolicyBudget &b) {
  for (auto n :
       {b.principals, b.rules, b.targets, b.sessions, b.active_actions,
        b.active_responses, b.active_watches, b.members, b.watches_per_session,
        b.watches_per_principal, b.diagnostics, b.declarations, b.text_bytes,
        b.credential_bytes, b.queued_frames, b.queued_bytes, b.frame_bytes,
        b.page_size, b.scan_limit, b.inline_bindings, b.active_inline_calls})
    if (!n)
      return false;
  return b.identity_limit && b.generation_limit && b.page_size <= 200 &&
         b.frame_bytes <= b.queued_bytes && b.session_ttl.count() > 0 &&
         b.action_ttl.count() > 0 && b.page_ttl.count() > 0 &&
         b.queued_ttl.count() > 0;
}
Result<TimePoint> expires(TimePoint now, std::chrono::milliseconds ttl) {
  auto count = ttl.count();
  using D = TimePoint::duration;
  if (count <= 0 || count > (std::numeric_limits<D::rep>::max)() / 1000000)
    return failure<TimePoint>(PolicyErrc::InvalidInput);
  auto delta = std::chrono::duration_cast<D>(ttl);
  if (now.time_since_epoch().count() >
      (std::numeric_limits<D::rep>::max)() - delta.count())
    return failure<TimePoint>(PolicyErrc::InvalidInput);
  return now + delta;
}
Usage config_usage(const PolicyConfiguration &c, const PolicyBudget &b,
                   bool &good) {
  Meter m{b};
  if (c.principals.size() > b.principals || c.targets.size() > b.targets ||
      c.operations.size() > b.rules || c.uses.size() > 10)
    m.valid = false;
  m.count(c.principals.size());
  m.count(c.operations.size());
  m.count(c.uses.size());
  m.count(c.targets.size());
  for (std::size_t i = 0; i < c.principals.size(); ++i) {
    auto &p = c.principals[i];
    if (p.principal.principal_id.empty())
      m.valid = false;
    for (std::size_t j = 0; j < i; ++j)
      if (c.principals[j].principal == p.principal)
        m.valid = false;
    m.rules(p.rules);
  }
  for (std::size_t i = 0; i < c.operations.size(); ++i) {
    auto &o = c.operations[i];
    m.selector(o.operation);
    m.count(o.required_permissions.size());
    for (auto &n : o.required_permissions)
      m.text(n.view());
    if (o.required_permissions.empty() || !unique(o.required_permissions))
      m.valid = false;
    for (std::size_t j = 0; j < i; ++j)
      if (c.operations[j].operation.operation == o.operation.operation)
        m.valid = false;
    m.rules(o.module_rules);
  }
  for (std::size_t i = 0; i < c.uses.size(); ++i) {
    auto &u = c.uses[i];
    if (u.use <= AccessUse::Invoke || u.use > AccessUse::Subscribe ||
        u.required_permissions.empty() || !unique(u.required_permissions))
      m.valid = false;
    for (std::size_t j = 0; j < i; ++j)
      if (c.uses[j].use == u.use)
        m.valid = false;
    m.count(u.required_permissions.size());
    for (auto &n : u.required_permissions)
      m.text(n.view());
    m.rules(u.module_rules);
  }
  for (std::size_t i = 0; i < c.targets.size(); ++i) {
    auto &t = c.targets[i];
    if (t.target.empty() || !t.lifecycle_generation ||
        t.lifecycle_generation > b.generation_limit ||
        !owned(t.lifetime_owner) ||
        dynamic_cast<ResourceLease *>(t.lifetime_owner.get()) ||
        dynamic_cast<ActivityLease *>(t.lifetime_owner.get()))
      m.valid = false;
    for (std::size_t j = 0; j < i; ++j)
      if (c.targets[j].target == t.target)
        m.valid = false;
    m.rules(t.rules);
  }
  good = m.valid;
  return m.value;
}
Result<void> validate_configuration(PolicyBudget budget, const PolicyConfiguration& input) {
  if (!valid_budget(budget)) return failure<void>(PolicyErrc::InvalidInput);
  bool good = false;
  (void)config_usage(input, budget, good);
  if (!good) return failure<void>(PolicyErrc::BudgetExceeded);
  return {};
}
std::atomic<std::uint64_t> store_sequence{0};
Result<std::uint64_t> issue_process() {
  auto n = store_sequence.load();
  for (;;) {
    if (n == (std::numeric_limits<std::uint64_t>::max)())
      return failure<std::uint64_t>(PolicyErrc::IdentityExhausted);
    if (store_sequence.compare_exchange_weak(n, n + 1))
      return n + 1;
  }
}
template <class T> T identity(std::uint64_t store, std::uint64_t child) {
  T result{};
  for (unsigned i = 0; i < 8; ++i) {
    result.bytes[7 - i] = static_cast<std::uint8_t>(store >> (i * 8));
    result.bytes[15 - i] = static_cast<std::uint8_t>(child >> (i * 8));
  }
  return result;
}
struct Store;
struct Session;
struct Action;
struct Response;
struct Watch;
struct Coordinator;
struct QueueState;
enum class CountKind { Session, Action, Response, Watch, InlineBinding, Other };
struct Hold {
  std::shared_ptr<Store> store;
  Usage usage;
  CountKind kind = CountKind::Other;
  bool counted = false;
  ~Hold();
};
struct TargetStamp {
  ObjectId target;
  TargetInstanceId instance;
  std::uint64_t lifecycle;
  std::shared_ptr<PortLifetime> owner;
};
struct TargetIdentity {
  ObjectId target;
  TargetInstanceId instance;
  std::uint64_t lifecycle;
};
struct Store {
  PolicyBudget budget;
  std::mutex mutex;
  bool closed = false;
  std::uint64_t serial = 0, sequence = 0, generation = 1;
  Usage used;
  std::size_t live_sessions = 0, live_actions = 0, live_responses = 0,
              live_watches = 0, queued = 0, bytes = 0,
              live_inline_bindings = 0, active_inline_calls = 0;
  std::shared_ptr<const PolicyConfiguration> config;
  std::vector<TargetIdentity> target_ids = std::vector<TargetIdentity>(0);
  std::vector<std::pair<PrincipalRef, std::size_t>> watch_counts =
      std::vector<std::pair<PrincipalRef, std::size_t>>(0);
  std::vector<std::shared_ptr<QueueState>> coordinators =
      std::vector<std::shared_ptr<QueueState>>(0);
  std::shared_ptr<TrustedAuthenticationPort> auth;
  std::shared_ptr<ClockPort> clock;
  std::shared_ptr<TrustedGroupDigestPort> digest;
  std::shared_ptr<ExecutionAccessSourcePort> source;
  ObservationSourceIdentity source_id;
};
std::size_t &counter(Store &s, CountKind k) {
  if (k == CountKind::Session)
    return s.live_sessions;
  if (k == CountKind::Action)
    return s.live_actions;
  if (k == CountKind::Response)
    return s.live_responses;
  if (k == CountKind::InlineBinding)
    return s.live_inline_bindings;
  return s.live_watches;
}
std::size_t maximum(const Store &s, CountKind k) {
  if (k == CountKind::Session)
    return s.budget.sessions;
  if (k == CountKind::Action)
    return s.budget.active_actions;
  if (k == CountKind::Response)
    return s.budget.active_responses;
  if (k == CountKind::InlineBinding)
    return s.budget.inline_bindings;
  return s.budget.active_watches;
}
Hold::~Hold() {
  if (counted) {
    std::lock_guard lock(store->mutex);
    store->used.declarations -= usage.declarations;
    store->used.text -= usage.text;
    if (kind != CountKind::Other)
      --counter(*store, kind);
  }
}
Result<void> acquire(Hold &h) {
  auto &s = *h.store;
  if (s.closed)
    return deny(PolicyErrc::StoreClosed);
  if ((h.kind != CountKind::Other &&
       counter(s, h.kind) >= maximum(s, h.kind)) ||
      h.usage.declarations > s.budget.declarations - s.used.declarations ||
      h.usage.text > s.budget.text_bytes - s.used.text)
    return deny(PolicyErrc::BudgetExceeded);
  s.used.declarations += h.usage.declarations;
  s.used.text += h.usage.text;
  if (h.kind != CountKind::Other)
    ++counter(s, h.kind);
  h.counted = true;
  return {};
}
Result<std::uint64_t> next(Store &s) {
  if (s.sequence >= s.budget.identity_limit)
    return failure<std::uint64_t>(PolicyErrc::IdentityExhausted);
  return ++s.sequence;
}
Result<void> bump(Store &s) {
  if (s.generation >= s.budget.generation_limit)
    return deny(PolicyErrc::GenerationExhausted);
  ++s.generation;
  return {};
}
Result<void> source_current(const std::shared_ptr<Store> &s);
struct Session {
  Hold hold;
  ConnectionId id;
  PrincipalRef principal;
  PrincipalKind kind;
  std::optional<PrincipalRef> delegated_by;
  std::shared_ptr<const DelegationInput> scope, ceiling;
  TimePoint deadline;
  std::uint64_t generation = 1;
  bool closed = false;
  CallerAuthorityPort *caller_port = nullptr;
  TargetAuthorityPort *target_port = nullptr;
  std::size_t watches = 0;
};
struct SessionEntity {
  std::shared_ptr<Session> session;
  std::shared_ptr<CallerAuthorityPort> callers;
  std::shared_ptr<TargetAuthorityPort> targets;
  std::shared_ptr<ObservationAuthorization> observation;
};
Result<void> current(const Session &s) {
  if (s.hold.store->closed)
    return deny(PolicyErrc::StoreClosed);
  if (s.closed)
    return deny(PolicyErrc::SessionClosed);
  if (s.hold.store->clock->now() >= s.deadline)
    return deny(PolicyErrc::Expired);
  return {};
}
bool contained_rule(const ScopeRule &a, const ScopeRule &b) {
  return a.use == b.use && (!b.operation || a.operation == b.operation) &&
         subset(a.permissions, b.permissions) && subset(a.targets, b.targets) &&
         subset(a.owners, b.owners) && subset(a.fields, b.fields);
}
bool narrower(const DelegationInput &a, const DelegationInput &b) {
  if (a.deadline > b.deadline || a.allow_redelegation)
    return false;
  for (auto &r : a.rules) {
    bool found = false;
    for (auto &old : b.rules)
      if (contained_rule(r, old))
        found = true;
    if (!found)
      return false;
  }
  return true;
}
const OperationPolicyInput *operation_policy(const Store &s,
                                             const OperationSelector &o) {
  for (auto &x : s.config->operations)
    if (x.operation == o)
      return &x;
  return nullptr;
}
const TargetPolicyInput *target_policy(const Store &s, ObjectId t) {
  for (auto &x : s.config->targets)
    if (x.target == t)
      return &x;
  return nullptr;
}
const UsePolicyInput *use_policy(const Store &s, AccessUse use) {
  for (auto &x : s.config->uses)
    if (x.use == use)
      return &x;
  return nullptr;
}
const PrincipalPolicyInput *principal_policy(const Store &s, PrincipalRef p) {
  for (auto &x : s.config->principals)
    if (x.principal == p)
      return &x;
  return nullptr;
}
bool match(const std::vector<ScopeRule> &rules, AccessUse use,
           const OperationSelector &o, ObjectId target, PrincipalRef owner,
           const std::vector<Name> &permissions,
           std::optional<SummaryField> field, bool preowner = false) {
  for (auto &r : rules) {
    if (r.use != use || !subset(permissions, r.permissions))
      continue;
    if (preowner) {
      if (has(r.owners, owner))
        return true;
      continue;
    }
    if (r.operation && *r.operation != o)
      continue;
    if (!has(r.targets, target))
      continue;
    if (use != AccessUse::Invoke && use != AccessUse::Catalog &&
        !has(r.owners, owner))
      continue;
    if (field && !has(r.fields, *field))
      continue;
    return true;
  }
  return false;
}
bool allowed(const Session &session, AccessUse use, const OperationSelector &o,
             ObjectId t, PrincipalRef owner,
             std::optional<SummaryField> field = {}) {
  auto &s = *session.hold.store;
  auto *p = principal_policy(s, session.principal);
  auto *op = operation_policy(s, o);
  auto *target = target_policy(s, t);
  if (!p || !op || !target)
    return false;
  const std::vector<Name> *permissions = &op->required_permissions;
  const UsePolicyInput *u = nullptr;
  if (use != AccessUse::Invoke) {
    u = use_policy(s, use);
    if (!u)
      return false;
    permissions = &u->required_permissions;
  }
  return match(p->rules, use, o, t, owner, *permissions, field) &&
         match(session.scope->rules, use, o, t, owner, *permissions, field) &&
         match(session.ceiling->rules, use, o, t, owner, *permissions, field) &&
         match(op->module_rules, use, o, t, owner, *permissions, field) &&
         match(target->rules, use, o, t, owner, *permissions, field) &&
         (!u || match(u->module_rules, use, o, t, owner, *permissions, field));
}
bool target_candidate(const Session &session, ObjectId id) {
  auto &s = *session.hold.store;
  auto *p = principal_policy(s, session.principal);
  auto *t = target_policy(s, id);
  if (!p || !t)
    return false;
  auto matches = [&](const std::vector<ScopeRule> &rules, AccessUse use,
                     const OperationSelector &op,
                     const std::vector<Name> &required) {
    for (auto &r : rules)
      if (r.use == use && (!r.operation || *r.operation == op) &&
          has(r.targets, id) && subset(required, r.permissions))
        return true;
    return false;
  };
  for (auto &op : s.config->operations)
    for (int index = 0; index <= static_cast<int>(AccessUse::Subscribe);
         ++index) {
      auto use = static_cast<AccessUse>(index);
      auto *u = use == AccessUse::Invoke ? nullptr : use_policy(s, use);
      if (use != AccessUse::Invoke && !u)
        continue;
      auto &required = u ? u->required_permissions : op.required_permissions;
      if (matches(p->rules, use, op.operation, required) &&
          matches(session.scope->rules, use, op.operation, required) &&
          matches(session.ceiling->rules, use, op.operation, required) &&
          matches(op.module_rules, use, op.operation, required) &&
          matches(t->rules, use, op.operation, required) &&
          (!u || matches(u->module_rules, use, op.operation, required)))
        return true;
    }
  return false;
}
bool owner_candidate(const Session &session, AccessUse use,
                     PrincipalRef owner) {
  auto &s = *session.hold.store;
  auto *p = principal_policy(s, session.principal);
  auto *u = use_policy(s, use);
  if (!p || !u || s.config->operations.empty())
    return false;
  auto &o = s.config->operations[0].operation;
  ObjectId t{};
  return match(p->rules, use, o, t, owner, u->required_permissions, {}, true) &&
         match(session.scope->rules, use, o, t, owner, u->required_permissions,
               {}, true) &&
         match(session.ceiling->rules, use, o, t, owner,
               u->required_permissions, {}, true) &&
         match(u->module_rules, use, o, t, owner, u->required_permissions, {},
               true);
}
std::optional<TargetStamp> stamp(const Store &s, ObjectId t) {
  auto *p = target_policy(s, t);
  if (!p)
    return {};
  for (auto &i : s.target_ids)
    if (i.target == t)
      return TargetStamp{t, i.instance, p->lifecycle_generation,
                         p->lifetime_owner};
  return {};
}
bool stamp_current(const Store &s, const TargetStamp &t) {
  auto now = stamp(s, t.target);
  return now && now->instance == t.instance && now->lifecycle == t.lifecycle;
}
class Grant final : public CallerGrant {
public:
  Hold hold;
  std::shared_ptr<Session> session;
  CallerAuthorityPort *issuer;
  std::uint64_t generation;
  CallerDescription description_;
  Grant(std::shared_ptr<Session> s, CallerAuthorityPort *i)
      : session(std::move(s)), issuer(i), generation(0),
        description_{session->principal, session->delegated_by,
                     std::vector<Name>(std::initializer_list<Name>{})} {
    hold.store = session->hold.store;
    hold.usage.declarations = 1;
  }
  const CallerDescription &description() const noexcept override {
    return description_;
  }
};
class CallerPort final : public CallerAuthorityPort {
  std::shared_ptr<Session> s_;

public:
  explicit CallerPort(std::shared_ptr<Session> s) : s_(std::move(s)) {}
  Result<std::shared_ptr<const CallerGrant>>
  authenticate(const CallerDescription &d) override {
    try {
      auto grant = std::make_shared<Grant>(s_, this);
      std::lock_guard lock(s_->hold.store->mutex);
      auto c = current(*s_);
      if (!c)
        return make_unexpected(c.error());
      if (d.principal != s_->principal || d.delegated_by != s_->delegated_by ||
          !d.tags.empty())
        return failure<std::shared_ptr<const CallerGrant>>(
            PolicyErrc::AuthenticationFailed);
      grant->generation = s_->generation;
      c = acquire(grant->hold);
      if (!c)
        return make_unexpected(c.error());
      return std::shared_ptr<const CallerGrant>(std::move(grant));
    } catch (const std::bad_alloc &) {
      return failure<std::shared_ptr<const CallerGrant>>(
          PolicyErrc::BudgetExceeded);
    }
  }
  Result<void> validate(const CallerGrant &g) const override {
    std::lock_guard lock(s_->hold.store->mutex);
    auto c = current(*s_);
    if (!c)
      return c;
    auto *p = dynamic_cast<const Grant *>(&g);
    if (!p || p->session != s_ || p->issuer != this ||
        p->generation != s_->generation)
      return deny(PolicyErrc::InvalidAuthority);
    return {};
  }
};
class Target final : public TargetView {
public:
  Hold hold;
  std::shared_ptr<Session> session;
  TargetStamp value;
  std::uint64_t permission, delegation;
  TargetAuthorityPort *issuer;
  Target(std::shared_ptr<Session> s, TargetStamp t, TargetAuthorityPort *i)
      : session(std::move(s)), value(std::move(t)), permission(0),
        delegation(0), issuer(i) {
    hold.store = session->hold.store;
    hold.usage.declarations = 1;
  }
  ObjectId target() const noexcept override { return value.target; }
};
class TargetPort final : public TargetAuthorityPort {
  std::shared_ptr<Session> s_;

public:
  explicit TargetPort(std::shared_ptr<Session> s) : s_(std::move(s)) {}
  Result<std::shared_ptr<const TargetView>>
  resolve(const CallerView &caller, ObjectId requested) override;
  Result<void> validate(const TargetView &v, const CallerView &caller,
                        ObjectId expected) const override;
};
struct Verified {
  std::shared_ptr<Session> session;
  std::shared_ptr<const CallerGrant> grant;
  CallerView view;
  std::shared_ptr<CallerAuthorityPort> authority;
};
// Hold 最后析构：其余 owner 已在 Store 锁外释放，活跃 Admission
// 持有整个记录，确保业务、结果处理完成之前不会退还绑定计费。
struct InlineRecord {
  Hold hold;
  std::shared_ptr<SessionEntity> entity;
  std::shared_ptr<Verified> caller;
  std::shared_ptr<const DefinitionSnapshot> definition;
  OperationSelector selector;
  std::vector<std::shared_ptr<const Target>> targets;
  std::vector<unsigned char> slots;
  std::uint64_t permission = 0, delegation = 0;
  TimePoint deadline;
  InlineRecord(std::shared_ptr<SessionEntity> e, std::shared_ptr<Verified> c,
               std::shared_ptr<const DefinitionSnapshot> d)
      : entity(std::move(e)), caller(std::move(c)), definition(std::move(d)),
        selector{definition->description().key,
                 definition->description().contract_digest} {
    hold.store = entity->session->hold.store;
    hold.kind = CountKind::InlineBinding;
  }
};
struct Group {
  ActionRequest request;
};
enum class ActionStatus { Prepared, Issued, Consumed, Cancelled };
struct Action {
  Hold hold;
  ActionId id;
  std::shared_ptr<Verified> caller;
  std::shared_ptr<const GroupSnapshot> group;
  const ActionRequest &request;
  ContractDigest digest;
  std::vector<TargetStamp> targets = std::vector<TargetStamp>(0);
  PermitBinding binding;
  std::uint64_t delegation;
  ActionStatus status = ActionStatus::Prepared;
  std::shared_ptr<const ActionPermit> permit;
  explicit Action(const ActionRequest &r)
      : request(r),
        binding{{}, request.envelope.operation, {}, request.anchor_target, 0,
                0,  request.requested_deadline},
        delegation(0) {};
};
class Permit final : public ActionPermit {
public:
  Hold hold;
  std::weak_ptr<Action> action;
  const PermitBinding value;
  Permit(std::shared_ptr<Action> a) : action(a), value(a->binding) {
    hold.store = a->hold.store;
    hold.usage = {1, value.operation.name.view().size() +
                         value.operation.version.text().size()};
  }
  const PermitBinding &binding() const noexcept override { return value; }
};
struct Entry {
  ExecutionAccessInput original;
  OperationSelector operation;
  std::vector<TargetStamp> targets = std::vector<TargetStamp>(0);
  std::vector<SummaryField> fields = std::vector<SummaryField>(0);
};
struct Projection {
  ProjectionKind kind;
  std::variant<std::shared_ptr<const ExecutionSummary>, ListPage, ChangeHint>
      value;
};
enum class ResponseStatus { Fresh, Queued, Started, Dropped, Unknown };
struct Response {
  Hold hold;
  ResponseId id;
  std::shared_ptr<Session> session;
  AccessUse use;
  TimePoint deadline;
  std::shared_ptr<const ProjectionSnapshot> projection;
  std::vector<Entry> entries =
      std::vector<Entry>(std::initializer_list<Entry>{});
  ObservationSourceIdentity source;
  std::uint64_t permission, delegation;
  ResponseStatus status = ResponseStatus::Fresh;
};
struct Watch {
  Hold hold;
  WatchKey key;
  std::uint64_t generation = 1;
  std::shared_ptr<Session> session;
  ObservationFilter filter{
      std::vector<ExecutionRef>(0), {}, std::vector<ObservationTopic>(0)};
  TimePoint deadline;
  ObservationSourceIdentity source;
  std::vector<Entry> entries =
      std::vector<Entry>(std::initializer_list<Entry>{});
  bool closed = false;
  ~Watch() {
    if (hold.counted) {
      std::lock_guard lock(hold.store->mutex);
      --session->watches;
      for (auto i = hold.store->watch_counts.begin();
           i != hold.store->watch_counts.end(); ++i)
        if (i->first == session->principal) {
          if (--i->second == 0)
            hold.store->watch_counts.erase(i);
          break;
        }
    }
  }
};
struct Observation {
  std::shared_ptr<Session> session;
};
struct Frame {
  std::shared_ptr<Frame> retired_next;
  Hold hold;
  std::shared_ptr<const ResponseAuthorization> response;
  std::shared_ptr<Watch> watch;
  std::shared_ptr<const ProjectionSnapshot> projection;
  std::vector<Entry> entries =
      std::vector<Entry>(std::initializer_list<Entry>{});
  std::unique_ptr<PreparedTransmission> transmission;
  std::shared_ptr<TransmissionStartPort> reservation_owner;
  std::unique_ptr<TransmissionReservation> reservation;
  TimePoint deadline;
  std::uint64_t permission, delegation;
  bool charged = false;
};
struct QueueState {
  const Session *session = nullptr;
  std::vector<std::shared_ptr<Frame>> queue =
      std::vector<std::shared_ptr<Frame>>(0);
};
struct Coordinator {
  Hold hold;
  std::shared_ptr<Session> session;
  std::shared_ptr<TransmissionStartPort> sink;
  std::shared_ptr<ProjectionEncoderPort> encoder;
  std::shared_ptr<QueueState> queues = std::make_shared<QueueState>();
  std::vector<std::shared_ptr<Frame>> &queue = queues->queue;
  std::mutex pumping;
  ~Coordinator();
};
} // namespace detail
#define POLICY_STATE(N, R)                                                     \
  struct N::State {                                                            \
    std::shared_ptr<detail::R> value;                                          \
  };                                                                           \
  N::N(std::shared_ptr<State> s) : state_(std::move(s)) {}                     \
  N::~N() = default
POLICY_STATE(PolicyStore, Store);
POLICY_STATE(PolicyAdministration, Store);
POLICY_STATE(VerifiedCaller, Verified);
POLICY_STATE(GroupSnapshot, Group);
POLICY_STATE(ActionAuthorization, Action);
POLICY_STATE(InlineAuthorization, InlineRecord);
POLICY_STATE(ObservationAuthorization, Observation);
POLICY_STATE(ResponseAuthorization, Response);
POLICY_STATE(ProjectionSnapshot, Projection);
POLICY_STATE(SendCoordinator, Coordinator);
#undef POLICY_STATE
struct WatchAuthorization::State {
  std::shared_ptr<detail::Watch> value;
};
WatchAuthorization::WatchAuthorization(std::shared_ptr<State> s)
    : state_(std::move(s)) {}
struct SessionAuthority::State {
  std::shared_ptr<detail::SessionEntity> value;
};
SessionAuthority::SessionAuthority(std::shared_ptr<State> s)
    : state_(std::move(s)) {}
SessionAuthority::~SessionAuthority() { (void)close(); }
namespace detail {
struct Access {
  template <class T> static auto record(const T &v) { return v.state_->value; }
  template <class T, class R>
  static std::shared_ptr<T> make(std::shared_ptr<R> r) {
    return std::shared_ptr<T>(new T(
        std::make_shared<typename T::State>(typename T::State{std::move(r)})));
  }
  template <class T, class R>
  static std::unique_ptr<T> make_unique(std::shared_ptr<R> r) {
    return std::unique_ptr<T>(new T(
        std::make_shared<typename T::State>(typename T::State{std::move(r)})));
  }
  static PageBinding page(PageBindingData d) {
    return PageBinding(std::move(d));
  }
  static std::unique_ptr<PreparedTransmission>
  transmission(const std::vector<std::byte> &b, TransmissionBinding t,
               std::shared_ptr<const ProjectionSnapshot> p) {
    return std::unique_ptr<PreparedTransmission>(
        new PreparedTransmission(b, std::move(t), std::move(p)));
  }
};
} // namespace detail

namespace detail {
std::shared_ptr<Frame> drain(Store &s, const Session *session = nullptr,
                             const Watch *watch = nullptr) {
  std::shared_ptr<Frame> retired;
  for (auto &c : s.coordinators) {
    if (session && c->session != session)
      continue;
    for (std::size_t i = 0; i < c->queue.size();) {
      if (watch && c->queue[i]->watch.get() != watch) {
        ++i;
        continue;
      }
      auto f = std::move(c->queue[i]);
      c->queue.erase(c->queue.begin() + i);
      if (f->charged) {
        --s.queued;
        s.bytes -= f->transmission->bytes().size();
        f->charged = false;
      }
      if (f->response)
        Access::record(*f->response)->status = ResponseStatus::Dropped;
      f->retired_next = std::move(retired);
      retired = std::move(f);
    }
  }
  return retired;
}
Result<void> source_current(const std::shared_ptr<Store> &s) {
  const auto actual = s->source->identity();
  std::shared_ptr<Frame> retired;
  bool closed;
  {
    std::lock_guard lock(s->mutex);
    if (actual != s->source_id) {
      s->closed = true;
      retired = drain(*s);
    }
    closed = s->closed;
  }
  return closed ? deny(PolicyErrc::StoreClosed) : Result<void>{};
}
} // namespace detail
WatchAuthorization::~WatchAuthorization() {
  auto w = state_->value;
  std::shared_ptr<detail::Frame> retired;
  {
    std::lock_guard lock(w->hold.store->mutex);
    w->closed = true;
    retired = detail::drain(*w->hold.store, w->session.get(), w.get());
  }
}
using namespace detail;
const OperationSelector &GroupSnapshot::envelope() const noexcept {
  return state_->value->request.envelope;
}
ObjectId GroupSnapshot::anchor_target() const noexcept {
  return state_->value->request.anchor_target;
}
std::span<const MemberRequest> GroupSnapshot::members() const noexcept {
  return state_->value->request.members;
}
const CallerView &VerifiedCaller::view() const noexcept {
  return state_->value->view;
}
std::shared_ptr<CallerAuthorityPort>
VerifiedCaller::authority() const noexcept {
  return state_->value->authority;
}
std::shared_ptr<CallerAuthorityPort>
SessionAuthority::callers() const noexcept {
  return state_->value->callers;
}
std::shared_ptr<TargetAuthorityPort>
SessionAuthority::targets() const noexcept {
  return state_->value->targets;
}
std::shared_ptr<ObservationAuthorization>
SessionAuthority::observations() const noexcept {
  return state_->value->observation;
}
ProjectionKind ProjectionSnapshot::kind() const noexcept {
  return state_->value->kind;
}
const std::variant<std::shared_ptr<const ExecutionSummary>, ListPage,
                   ChangeHint> &
ProjectionSnapshot::value() const noexcept {
  return state_->value->value;
}
const ProjectionSnapshot &ResponseAuthorization::projection() const noexcept {
  return *state_->value->projection;
}
AccessUse ResponseAuthorization::use() const noexcept {
  return state_->value->use;
}
TimePoint ResponseAuthorization::deadline() const noexcept {
  return state_->value->deadline;
}
WatchKey WatchAuthorization::key() const noexcept { return state_->value->key; }
std::uint64_t WatchAuthorization::generation() const noexcept {
  return state_->value->generation;
}
const ObservationFilter &WatchAuthorization::filter() const noexcept {
  return state_->value->filter;
}
TimePoint WatchAuthorization::deadline() const noexcept {
  return state_->value->deadline;
}

Result<PolicyAssembly>
PolicyStore::create(PolicyBudget b, const PolicyConfiguration &input,
                    std::shared_ptr<TrustedAuthenticationPort> auth,
                    std::shared_ptr<ClockPort> clock,
                    std::shared_ptr<TrustedGroupDigestPort> digest,
                    std::shared_ptr<ExecutionAccessSourcePort> source) {
  try {
    if (!valid_budget(b) || !owned(auth) || !owned(clock) || !owned(digest) ||
        !owned(source))
      return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
    auto sid = source->identity();
    if (sid.host.empty() || sid.restore != RestoreMode::Absent)
      return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
    bool good;
    auto usage = config_usage(input, b, good);
    if (!good)
      return failure<PolicyAssembly>(PolicyErrc::BudgetExceeded);
    for (auto ttl : {b.session_ttl, b.action_ttl, b.page_ttl, b.queued_ttl})
      if (!expires(clock->now(), ttl))
        return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
    auto s = std::make_shared<Store>();
    s->budget = b;
    s->watch_counts.reserve(b.active_watches);
    s->coordinators.reserve(b.sessions);
    s->config = std::make_shared<const PolicyConfiguration>(input);
    s->used = usage;
    s->auth = std::move(auth);
    s->clock = std::move(clock);
    s->digest = std::move(digest);
    s->source = std::move(source);
    s->source_id = sid;
    auto serial = issue_process();
    if (!serial)
      return make_unexpected(serial.error());
    s->serial = *serial;
    s->target_ids.reserve(input.targets.size());
    for (auto &t : input.targets) {
      auto n = next(*s);
      if (!n)
        return make_unexpected(n.error());
      s->target_ids.push_back({t.target,
                               identity<TargetInstanceId>(s->serial, *n),
                               t.lifecycle_generation});
    }
    return PolicyAssembly{Access::make<PolicyStore>(s),
                          Access::make_unique<PolicyAdministration>(s)};
  } catch (const std::bad_alloc &) {
    return failure<PolicyAssembly>(PolicyErrc::BudgetExceeded);
  }
}
Result<std::shared_ptr<SessionAuthority>>
PolicyStore::open(const AuthenticationAttempt &attempt,
                  const DelegationInput &requested) {
  try {
    auto s = state_->value;
    auto check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    if (attempt.credential.empty() ||
        attempt.credential.size() > s->budget.credential_bytes)
      return failure<std::shared_ptr<SessionAuthority>>(
          PolicyErrc::BudgetExceeded);
    Meter meter{s->budget};
    meter.rules(requested.rules);
    if (!meter.valid)
      return failure<std::shared_ptr<SessionAuthority>>(
          PolicyErrc::BudgetExceeded);
    auto scope = std::make_shared<const DelegationInput>(requested);
    AuthenticationAttempt credentials = attempt;
    auto authenticated = s->auth->authenticate(credentials);
    if (!authenticated)
      return make_unexpected(authenticated.error());
    auto &v = *authenticated;
    if (v.principal.principal_id.empty() || v.kind < PrincipalKind::User ||
        v.kind > PrincipalKind::Service || scope->allow_redelegation ||
        v.ceiling.allow_redelegation)
      return failure<std::shared_ptr<SessionAuthority>>(
          PolicyErrc::UnsupportedDelegation);
    meter.rules(v.ceiling.rules);
    if (!meter.valid)
      return failure<std::shared_ptr<SessionAuthority>>(
          PolicyErrc::BudgetExceeded);
    auto ttl = expires(s->clock->now(), s->budget.session_ttl);
    if (!ttl)
      return make_unexpected(ttl.error());
    auto deadline =
        (std::min)({*ttl, scope->deadline, v.deadline, v.ceiling.deadline});
    auto session = std::make_shared<Session>();
    session->hold.store = s;
    session->hold.usage = meter.value;
    session->hold.kind = CountKind::Session;
    session->principal = v.principal;
    session->kind = v.kind;
    session->delegated_by = v.delegated_by;
    session->scope = std::move(scope);
    session->ceiling = std::make_shared<const DelegationInput>(v.ceiling);
    session->deadline = deadline;
    auto e = std::make_shared<SessionEntity>();
    e->session = session;
    e->callers = std::make_shared<CallerPort>(session);
    e->targets = std::make_shared<TargetPort>(session);
    e->observation = Access::make<ObservationAuthorization>(
        std::make_shared<Observation>(Observation{session}));
    session->caller_port = e->callers.get();
    session->target_port = e->targets.get();
    auto result = Access::make<SessionAuthority>(e);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    {
      std::lock_guard lock(s->mutex);
      check = current(*session);
      if (!check)
        return make_unexpected(check.error());
      if (!principal_policy(*s, session->principal))
        return failure<std::shared_ptr<SessionAuthority>>(
            PolicyErrc::AuthenticationFailed);
      auto n = next(*s);
      if (!n)
        return make_unexpected(n.error());
      session->id = identity<ConnectionId>(s->serial, *n);
      check = acquire(session->hold);
      if (!check)
        return make_unexpected(check.error());
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<SessionAuthority>>(
        PolicyErrc::BudgetExceeded);
  }
}
Result<void> PolicyAdministration::close_store() {
  auto s = state_->value;
  std::shared_ptr<Frame> retired;
  {
    std::lock_guard lock(s->mutex);
    s->closed = true;
    retired = drain(*s);
  }
  return {};
}
Result<void> SessionAuthority::close() {
  auto s = state_->value->session;
  std::shared_ptr<Frame> retired;
  {
    std::lock_guard lock(s->hold.store->mutex);
    s->closed = true;
    retired = drain(*s->hold.store, s.get());
  }
  return {};
}
Result<std::shared_ptr<const VerifiedCaller>>
SessionAuthority::verify(const CallerDescription &d) {
  try {
    auto e = state_->value;
    auto g = e->callers->authenticate(d);
    if (!g)
      return make_unexpected(g.error());
    auto view = CallerView::check(e->callers, *g);
    if (!view)
      return make_unexpected(view.error());
    auto v =
        std::make_shared<Verified>(Verified{e->session, *g, *view, e->callers});
    return std::shared_ptr<const VerifiedCaller>(
        Access::make<VerifiedCaller>(v));
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<const VerifiedCaller>>(
        PolicyErrc::BudgetExceeded);
  }
}
Result<void>
SessionAuthority::restrict_delegation(const DelegationInput &input) {
  try {
    auto session = state_->value->session;
    auto s = session->hold.store;
    Meter m{s->budget};
    m.rules(input.rules);
    if (!m.valid)
      return deny(PolicyErrc::BudgetExceeded);
    auto candidate = std::make_shared<const DelegationInput>(input);
    std::shared_ptr<const DelegationInput> old;
    {
      std::lock_guard lock(s->mutex);
      old = session->scope;
    }
    Meter previous{s->budget};
    previous.rules(old->rules);
    {
      std::lock_guard lock(s->mutex);
      auto c = current(*session);
      if (!c)
        return c;
      if (session->scope != old)
        return deny(PolicyErrc::Busy);
      if (!narrower(*candidate, *old))
        return deny(PolicyErrc::UnsupportedDelegation);
      if (session->generation >= s->budget.generation_limit)
        return deny(PolicyErrc::GenerationExhausted);
      auto declarations = s->used.declarations - previous.value.declarations;
      auto text = s->used.text - previous.value.text;
      if (!charge(declarations, m.value.declarations, s->budget.declarations) ||
          !charge(text, m.value.text, s->budget.text_bytes))
        return deny(PolicyErrc::BudgetExceeded);
      s->used = {declarations, text};
      session->hold.usage.declarations = session->hold.usage.declarations -
                                         previous.value.declarations +
                                         m.value.declarations;
      session->hold.usage.text =
          session->hold.usage.text - previous.value.text + m.value.text;
      session->scope = std::move(candidate);
      ++session->generation;
      session->deadline =
          (std::min)(session->deadline, session->scope->deadline);
    }
    return {};
  } catch (const std::bad_alloc &) {
    return deny(PolicyErrc::BudgetExceeded);
  }
}
Result<std::shared_ptr<const TargetView>>
TargetPort::resolve(const CallerView &caller, ObjectId requested) {
  try {
    if (!caller.belongs_to(*s_->caller_port))
      return failure<std::shared_ptr<const TargetView>>(
          PolicyErrc::InvalidAuthority);
    auto cv = caller.revalidate();
    if (!cv)
      return make_unexpected(cv.error());
    auto store = s_->hold.store;
    std::optional<TargetStamp> captured;
    std::uint64_t generation, delegation;
    {
      std::lock_guard lock(store->mutex);
      auto c = current(*s_);
      if (!c)
        return make_unexpected(c.error());
      captured = stamp(*store, requested);
      if (!captured || !target_candidate(*s_, requested))
        return failure<std::shared_ptr<const TargetView>>(
            PolicyErrc::TargetUnavailable);
      generation = store->generation;
      delegation = s_->generation;
    }
    auto value = std::make_shared<Target>(s_, std::move(*captured), this);
    value->permission = generation;
    value->delegation = delegation;
    {
      std::lock_guard lock(store->mutex);
      auto c = current(*s_);
      if (!c)
        return make_unexpected(c.error());
      if (store->generation != generation || s_->generation != delegation ||
          !stamp_current(*store, value->value))
        return failure<std::shared_ptr<const TargetView>>(
            PolicyErrc::TargetUnavailable);
      c = acquire(value->hold);
      if (!c)
        return make_unexpected(c.error());
    }
    return std::shared_ptr<const TargetView>(value);
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<const TargetView>>(
        PolicyErrc::BudgetExceeded);
  }
}
Result<void> TargetPort::validate(const TargetView &v, const CallerView &caller,
                                  ObjectId expected) const {
  if (!caller.belongs_to(*s_->caller_port))
    return deny(PolicyErrc::InvalidAuthority);
  auto cv = caller.revalidate();
  if (!cv)
    return cv;
  auto store = s_->hold.store;
  std::lock_guard lock(store->mutex);
  auto c = current(*s_);
  if (!c)
    return c;
  auto *t = dynamic_cast<const Target *>(&v);
  if (!t || t->session != s_ || t->issuer != this ||
      t->value.target != expected || t->permission != store->generation ||
      t->delegation != s_->generation || !stamp_current(*store, t->value))
    return deny(PolicyErrc::InvalidAuthority);
  return {};
}

namespace detail {
Result<void> action_current(const Action &a) {
  auto &session = *a.caller->session;
  auto &s = *session.hold.store;
  auto c = current(session);
  if (!c)
    return c;
  if (a.status == ActionStatus::Cancelled)
    return deny(PolicyErrc::Cancelled);
  if (a.status == ActionStatus::Consumed)
    return deny(PolicyErrc::AlreadyConsumed);
  if (s.clock->now() >= a.binding.deadline)
    return deny(PolicyErrc::Expired);
  if (a.delegation != session.generation ||
      a.binding.permission_generation != s.generation)
    return deny(PolicyErrc::Denied);
  auto *g = dynamic_cast<const Grant *>(a.caller->grant.get());
  if (!g || g->session != a.caller->session ||
      g->issuer != session.caller_port || g->generation != session.generation)
    return deny(PolicyErrc::InvalidAuthority);
  for (auto &t : a.targets)
    if (!stamp_current(s, t))
      return deny(PolicyErrc::TargetUnavailable);
  if (!allowed(session, AccessUse::Invoke, a.request.envelope,
               a.request.anchor_target, session.principal))
    return deny(PolicyErrc::Denied);
  for (auto &m : a.request.members)
    for (auto t : m.targets)
      if (!allowed(session, AccessUse::Invoke, m.operation, t,
                   session.principal))
        return deny(PolicyErrc::Denied);
  return {};
}
Coordinator::~Coordinator() {
  if (session) {
    auto s = session->hold.store;
    {
      std::lock_guard lock(s->mutex);
      std::erase(s->coordinators, queues);
      for (auto &f : queue)
        if (f->charged) {
          --s->queued;
          s->bytes -= f->transmission->bytes().size();
          f->charged = false;
        }
    }
  }
}
} // namespace detail
Result<std::shared_ptr<ActionAuthorization>>
SessionAuthority::prepare(const VerifiedCaller &caller,
                          const ActionRequest &input) {
  try {
    auto session = state_->value->session;
    auto s = session->hold.store;
    auto verified = Access::record(caller);
    if (verified->session != session)
      return failure<std::shared_ptr<ActionAuthorization>>(
          PolicyErrc::InvalidAuthority);
    Meter meter{s->budget};
    meter.selector(input.envelope);
    meter.selector(input.envelope);
    meter.count(input.members.size());
    if (input.members.empty() || input.members.size() > s->budget.members ||
        input.anchor_target.empty())
      return failure<std::shared_ptr<ActionAuthorization>>(
          PolicyErrc::InvalidGroup);
    std::size_t target_count = 1;
    for (auto &m : input.members) {
      meter.selector(m.operation);
      meter.count(m.targets.size());
      if (m.targets.empty() || m.targets.size() > s->budget.targets ||
          m.targets.size() > s->budget.declarations - target_count)
        return failure<std::shared_ptr<ActionAuthorization>>(
            PolicyErrc::InvalidGroup);
      target_count += m.targets.size();
      for (auto t : m.targets)
        if (t.empty())
          return failure<std::shared_ptr<ActionAuthorization>>(
              PolicyErrc::InvalidGroup);
    }
    meter.count(target_count);
    if (!meter.valid)
      return failure<std::shared_ptr<ActionAuthorization>>(
          PolicyErrc::BudgetExceeded);
    auto request = input;
    for (auto &m : request.members) {
      std::sort(m.targets.begin(), m.targets.end(),
                [](auto &a, auto &b) { return a.bytes < b.bytes; });
      m.targets.erase(std::unique(m.targets.begin(), m.targets.end()),
                      m.targets.end());
    }
    auto group = Access::make<GroupSnapshot>(
        std::make_shared<Group>(Group{std::move(request)}));
    auto fingerprint = s->digest->fingerprint(*group);
    if (!fingerprint)
      return make_unexpected(fingerprint.error());
    auto a = std::make_shared<Action>(Access::record(*group)->request);
    a->hold.store = s;
    a->hold.kind = CountKind::Action;
    a->hold.usage = meter.value;
    a->caller = verified;
    a->group = group;
    a->digest = *fingerprint;
    a->targets.reserve(target_count);
    a->binding.group = *fingerprint;
    a->binding.principal = session->principal;
    auto ttl = expires(s->clock->now(), s->budget.action_ttl);
    if (!ttl)
      return make_unexpected(ttl.error());
    a->binding.deadline =
        (std::min)({input.requested_deadline, session->deadline, *ttl});
    auto result = Access::make<ActionAuthorization>(a);
    {
      std::lock_guard lock(s->mutex);
      auto c = current(*session);
      if (!c)
        return make_unexpected(c.error());
      auto *g = dynamic_cast<const Grant *>(verified->grant.get());
      if (!g || g->generation != session->generation)
        return failure<std::shared_ptr<ActionAuthorization>>(
            PolicyErrc::InvalidAuthority);
      auto *op = operation_policy(*s, a->request.envelope);
      if (!op)
        return failure<std::shared_ptr<ActionAuthorization>>(
            PolicyErrc::PolicyNotInstalled);
      const bool single =
          a->request.members.size() == 1 &&
          a->request.members[0].operation == a->request.envelope;
      if ((single &&
           !has(a->request.members[0].targets, a->request.anchor_target)) ||
          (!single && !op->permits_group))
        return failure<std::shared_ptr<ActionAuthorization>>(
            PolicyErrc::InvalidGroup);
      auto anchor = stamp(*s, a->request.anchor_target);
      if (!anchor)
        return failure<std::shared_ptr<ActionAuthorization>>(
            PolicyErrc::TargetUnavailable);
      a->targets.push_back(*anchor);
      a->binding.lifecycle_generation = anchor->lifecycle;
      a->binding.permission_generation = s->generation;
      a->delegation = session->generation;
      for (auto &m : a->request.members)
        for (auto t : m.targets) {
          auto actual = stamp(*s, t);
          if (!actual)
            return failure<std::shared_ptr<ActionAuthorization>>(
                PolicyErrc::TargetUnavailable);
          a->targets.push_back(*actual);
        }
      c = action_current(*a);
      if (!c)
        return make_unexpected(c.error());
      auto n = next(*s);
      if (!n)
        return make_unexpected(n.error());
      a->id = identity<ActionId>(s->serial, *n);
      c = acquire(a->hold);
      if (!c)
        return make_unexpected(c.error());
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<ActionAuthorization>>(
        PolicyErrc::BudgetExceeded);
  }
}
namespace detail {
// 仅在 Store 仲裁锁内调用；不构造 OperationSelector/string，不回调端口。
Result<void> inline_current(const InlineRecord &record) {
  const auto &session = record.entity->session;
  const auto &store = *record.hold.store;
  auto live = current(*session);
  if (!live)
    return live;
  if (store.clock->now() >= record.deadline)
    return deny(PolicyErrc::Expired);
  const auto &verified = record.caller;
  const auto *grant = dynamic_cast<const Grant *>(verified->grant.get());
  if (verified->session != session ||
      verified->authority.get() != session->caller_port ||
      !verified->view.belongs_to(*session->caller_port) || !grant ||
      grant->session != session || grant->issuer != session->caller_port ||
      grant->generation != session->generation ||
      record.delegation != session->generation ||
      record.permission != store.generation)
    return deny(PolicyErrc::InvalidAuthority);
  auto *installed = operation_policy(store, record.selector);
  if (!installed)
    return deny(PolicyErrc::PolicyNotInstalled);
  const auto &required = record.definition->description().required_permissions;
  if (installed->required_permissions.size() != required.size() ||
      !unique(required) || !unique(installed->required_permissions) ||
      !subset(required, installed->required_permissions))
    return deny(PolicyErrc::ContractMismatch);
  for (const auto &target : record.targets) {
    if (target->session != session || target->issuer != session->target_port ||
        target->permission != record.permission ||
        target->delegation != record.delegation)
      return deny(PolicyErrc::InvalidAuthority);
    // 直接只读身份表，避免临时 owning stamp 的析构进入短锁。
    const auto *policy = target_policy(store, target->value.target);
    bool present = false;
    for (const auto &identity : store.target_ids)
      if (identity.target == target->value.target &&
          identity.instance == target->value.instance &&
          identity.lifecycle == target->value.lifecycle)
        present = true;
    if (!policy || !present ||
        policy->lifecycle_generation != target->value.lifecycle)
      return deny(PolicyErrc::TargetUnavailable);
    if (!allowed(*session, AccessUse::Invoke, record.selector,
                 target->value.target, session->principal))
      return deny(PolicyErrc::Denied);
  }
  return {};
}
void release_inline_slot(InlineRecord &record, std::size_t slot) noexcept {
  std::lock_guard lock(record.hold.store->mutex);
  record.slots[slot] = 0;
  --record.hold.store->active_inline_calls;
}
} // namespace detail

Result<std::shared_ptr<const InlineAuthorization>>
SessionAuthority::prepare_inline(
    const VerifiedCaller &caller,
    std::shared_ptr<const DefinitionSnapshot> catalog_definition,
    std::span<const std::shared_ptr<const TargetView>> original_targets,
    std::size_t maximum_concurrent_calls) {
  using Output = std::shared_ptr<const InlineAuthorization>;
  try {
    auto entity = state_->value;
    auto store = entity->session->hold.store;
    auto verified = Access::record(caller);
    if (!owned(catalog_definition) || !owned(verified) ||
        !owned(verified->grant) || !owned(verified->authority))
      return failure<Output>(PolicyErrc::InvalidOwner);
    if (verified->session != entity->session)
      return failure<Output>(PolicyErrc::InvalidAuthority);
    if (catalog_definition->shape() != Shape::Read)
      return failure<Output>(PolicyErrc::ContractMismatch);
    if (original_targets.empty() || !maximum_concurrent_calls)
      return failure<Output>(PolicyErrc::InvalidInput);
    if (original_targets.size() > store->budget.targets ||
        maximum_concurrent_calls > store->budget.active_inline_calls ||
        original_targets.size() >
            (std::numeric_limits<std::size_t>::max)() /
                sizeof(std::shared_ptr<const Target>))
      return failure<Output>(PolicyErrc::BudgetExceeded);
    const auto &description = catalog_definition->description();
    Meter meter{store->budget};
    meter.count(1); // InlineRecord（包含冻结世代、期限、所有者引用）
    meter.count(original_targets.size());
    meter.count(maximum_concurrent_calls);
    meter.count(description.required_permissions.size());
    meter.count(1); // 原 DefinitionSnapshot
    meter.text(description.key.name.view());
    meter.text(description.key.version.text());
    // 独立保存的 selector 也计量其文本。
    meter.text(description.key.name.view());
    meter.text(description.key.version.text());
    meter.text(description.docs);
    meter.text(description.execution.executor.view());
    meter.text(description.execution.thread_affinity.view());
    for (const auto &permission : description.required_permissions)
      meter.text(permission.view());
    for (const auto *identity : {&catalog_definition->args_contract(),
                                 &catalog_definition->result_contract()}) {
      meter.count(1);
      meter.text(identity->name.view());
      meter.text(identity->version.text());
    }
    if (catalog_definition->provider()) {
      meter.count(1);
      meter.text(catalog_definition->provider()->view());
    }
    if (!meter.valid)
      return failure<Output>(PolicyErrc::BudgetExceeded);
    auto record = std::make_shared<InlineRecord>(
        entity, verified, std::move(catalog_definition));
    record->hold.usage = meter.value;
    record->targets.reserve(original_targets.size());
    record->slots.resize(maximum_concurrent_calls, 0);
    for (const auto &original : original_targets) {
      if (!owned(original))
        return failure<Output>(PolicyErrc::InvalidOwner);
      auto target = std::dynamic_pointer_cast<const Target>(original);
      if (!target || target->session != entity->session ||
          target->issuer != entity->targets.get())
        return failure<Output>(PolicyErrc::InvalidAuthority);
      if (!owned(target->value.owner))
        return failure<Output>(PolicyErrc::InvalidOwner);
      for (const auto &previous : record->targets)
        if (previous->value.target == target->value.target)
          return failure<Output>(PolicyErrc::InvalidInput);
      record->targets.push_back(std::move(target));
    }
    Output result = Access::make<InlineAuthorization>(record);
    {
      std::lock_guard lock(store->mutex);
      record->permission = store->generation;
      record->delegation = entity->session->generation;
      record->deadline = entity->session->deadline;
      auto check = inline_current(*record);
      if (!check)
        return make_unexpected(check.error());
      check = acquire(record->hold);
      if (!check)
        return make_unexpected(check.error());
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<Output>(PolicyErrc::BudgetExceeded);
  } catch (const std::length_error &) {
    return failure<Output>(PolicyErrc::BudgetExceeded);
  }
}

InlineAdmission::InlineAdmission(std::shared_ptr<InlineRecord> record,
                                 std::size_t slot, TimePoint deadline) noexcept
    : record_(std::move(record)), slot_(slot), deadline_(deadline) {}
InlineAdmission::InlineAdmission(InlineAdmission &&other) noexcept
    : record_(std::move(other.record_)), slot_(other.slot_),
      deadline_(other.deadline_) {}
InlineAdmission &InlineAdmission::operator=(InlineAdmission &&other) noexcept {
  if (this != &other) {
    if (record_)
      release_inline_slot(*record_, slot_);
    // 旧记录的最后 owner 析构发生于锁外。
    record_ = std::move(other.record_);
    slot_ = other.slot_;
    deadline_ = other.deadline_;
  }
  return *this;
}
InlineAdmission::~InlineAdmission() {
  if (record_)
    release_inline_slot(*record_, slot_);
}
TimePoint InlineAdmission::deadline() const noexcept { return deadline_; }
Result<InlineAdmission> InlineAuthorization::admit(
    const DefinitionSnapshot &expected_catalog_definition,
    std::span<const ObjectId> actual_targets, std::stop_token stop,
    TimePoint requested_deadline) const {
  auto record = state_->value;
  auto &store = *record->hold.store;
  if (&expected_catalog_definition != record->definition.get())
    return failure<InlineAdmission>(PolicyErrc::ContractMismatch);
  if (actual_targets.size() != record->targets.size())
    return failure<InlineAdmission>(PolicyErrc::InvalidInput);
  for (std::size_t index = 0; index < actual_targets.size(); ++index) {
    bool found = false;
    for (const auto &target : record->targets)
      if (target->value.target == actual_targets[index])
        found = true;
    if (!found)
      return failure<InlineAdmission>(PolicyErrc::InvalidInput);
    for (std::size_t previous = 0; previous < index; ++previous)
      if (actual_targets[previous] == actual_targets[index])
        return failure<InlineAdmission>(PolicyErrc::InvalidInput);
  }
  std::lock_guard lock(store.mutex);
  auto check = inline_current(*record);
  if (!check)
    return make_unexpected(check.error());
  const auto deadline = (std::min)(requested_deadline, record->deadline);
  if (store.clock->now() >= deadline)
    return failure<InlineAdmission>(PolicyErrc::Expired);
  std::size_t slot = 0;
  while (slot < record->slots.size() && record->slots[slot])
    ++slot;
  if (slot == record->slots.size())
    return failure<InlineAdmission>(PolicyErrc::Busy);
  if (store.active_inline_calls >= store.budget.active_inline_calls)
    return failure<InlineAdmission>(PolicyErrc::BudgetExceeded);
  // 紧邻计数改变的取消观察与同锁的撤权/关闭形成明确准入边界。
  if (stop.stop_requested())
    return failure<InlineAdmission>(PolicyErrc::Cancelled);
  record->slots[slot] = 1;
  ++store.active_inline_calls;
  return InlineAdmission(record, slot, deadline);
}
Result<std::shared_ptr<const ActionPermit>> ActionAuthorization::issue() {
  return issue(*state_->value->caller->grant, state_->value->binding);
}
Result<std::shared_ptr<const ActionPermit>>
ActionAuthorization::issue(const CallerGrant &grant,
                           const PermitBinding &binding) {
  try {
    auto a = state_->value;
    auto candidate = std::make_shared<Permit>(a);
    auto s = a->hold.store;
    std::lock_guard lock(s->mutex);
    auto c = action_current(*a);
    if (!c)
      return make_unexpected(c.error());
    if (&grant != a->caller->grant.get() || binding != a->binding)
      return failure<std::shared_ptr<const ActionPermit>>(
          PolicyErrc::InvalidPermit);
    if (!a->permit) {
      c = acquire(candidate->hold);
      if (!c)
        return make_unexpected(c.error());
      a->permit = candidate;
      a->status = ActionStatus::Issued;
    }
    return a->permit;
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<const ActionPermit>>(
        PolicyErrc::BudgetExceeded);
  }
}
Result<PermitBinding> ActionAuthorization::current_expected_binding() const {
  try {
    auto a = state_->value;
    auto candidate = a->binding;
    auto s = a->hold.store;
    std::lock_guard lock(s->mutex);
    auto c = action_current(*a);
    if (!c)
      return make_unexpected(c.error());
    return candidate;
  } catch (const std::bad_alloc &) {
    return failure<PermitBinding>(PolicyErrc::BudgetExceeded);
  }
}
Result<void> ActionAuthorization::consume(const ActionPermit &permit,
                                          const PermitBinding &expected) {
  auto a = state_->value;
  auto s = a->hold.store;
  std::lock_guard lock(s->mutex);
  auto c = action_current(*a);
  if (!c)
    return c;
  if (a->status != ActionStatus::Issued || &permit != a->permit.get() ||
      expected != a->binding)
    return deny(PolicyErrc::InvalidPermit);
  a->status = ActionStatus::Consumed;
  return {};
}
Result<void> ActionAuthorization::cancel() {
  auto a = state_->value;
  auto s = a->hold.store;
  std::lock_guard lock(s->mutex);
  if (s->closed)
    return deny(PolicyErrc::StoreClosed);
  if (a->status == ActionStatus::Consumed)
    return deny(PolicyErrc::AlreadyConsumed);
  a->status = ActionStatus::Cancelled;
  return {};
}

namespace detail {
template <class Change>
Result<void> update_config(const std::shared_ptr<Store> &s, Change change,
                           std::optional<ObjectId> replacement = {}) {
  try {
    std::shared_ptr<const PolicyConfiguration> old;
    std::vector<TargetIdentity> ids(0);
    ids.reserve(s->budget.targets);
    std::uint64_t epoch;
    {
      std::lock_guard lock(s->mutex);
      if (s->closed)
        return deny(PolicyErrc::StoreClosed);
      old = s->config;
      epoch = s->generation;
      for (auto &i : s->target_ids)
        ids.push_back(i);
    }
    auto candidate = std::make_shared<PolicyConfiguration>(*old);
    auto modified = change(*candidate);
    if (!modified)
      return modified;
    bool good;
    auto new_usage = config_usage(*candidate, s->budget, good);
    if (!good)
      return deny(PolicyErrc::BudgetExceeded);
    bool previous_good;
    auto previous = config_usage(*old, s->budget, previous_good);
    bool new_instance = false;
    std::size_t changed_index = 0;
    if (replacement) {
      auto t =
          std::find_if(candidate->targets.begin(), candidate->targets.end(),
                       [&](auto &t) { return t.target == *replacement; });
      if (t != candidate->targets.end()) {
        auto before =
            std::find_if(old->targets.begin(), old->targets.end(),
                         [&](auto &t) { return t.target == *replacement; });
        auto stamp = std::find_if(ids.begin(), ids.end(), [&](auto &i) {
          return i.target == *replacement;
        });
        new_instance =
            before == old->targets.end() ||
            before->lifetime_owner.get() != t->lifetime_owner.get() ||
            before->lifetime_owner.owner_before(t->lifetime_owner) ||
            t->lifetime_owner.owner_before(before->lifetime_owner);
        if (stamp != ids.end()) {
          if (t->lifecycle_generation < stamp->lifecycle ||
              (new_instance && t->lifecycle_generation <= stamp->lifecycle))
            return deny(PolicyErrc::InvalidInput);
          stamp->lifecycle = t->lifecycle_generation;
          changed_index = static_cast<std::size_t>(stamp - ids.begin());
        } else {
          if (ids.size() >= s->budget.targets)
            return deny(PolicyErrc::BudgetExceeded);
          changed_index = ids.size();
          ids.push_back({t->target, {}, t->lifecycle_generation});
        }
      }
    }
    {
      std::lock_guard lock(s->mutex);
      if (s->closed)
        return deny(PolicyErrc::StoreClosed);
      if (s->generation != epoch || s->config != old)
        return deny(PolicyErrc::Busy);
      if (s->generation >= s->budget.generation_limit)
        return deny(PolicyErrc::GenerationExhausted);
      auto declarations = s->used.declarations - previous.declarations;
      auto text = s->used.text - previous.text;
      if (!charge(declarations, new_usage.declarations,
                  s->budget.declarations) ||
          !charge(text, new_usage.text, s->budget.text_bytes))
        return deny(PolicyErrc::BudgetExceeded);
      if (new_instance) {
        auto n = next(*s);
        if (!n)
          return make_unexpected(n.error());
        ids[changed_index].instance = identity<TargetInstanceId>(s->serial, *n);
      }
      s->config = std::move(candidate);
      s->target_ids.swap(ids);
      s->used = {declarations, text};
      ++s->generation;
    }
    return {};
  } catch (const std::bad_alloc &) {
    return deny(PolicyErrc::BudgetExceeded);
  }
}

// 不可信输入先按不可变预算计量，禁止在复制大规则容器后才发现超限。
template <class T>
Result<void> preflight_input(const std::shared_ptr<Store> &s, const T &v) {
  {
    std::lock_guard lock(s->mutex);
    if (s->closed)
      return deny(PolicyErrc::StoreClosed);
  }
  Meter m{s->budget};
  m.count(1);
  if constexpr (std::is_same_v<T, PrincipalPolicyInput>) {
    if (v.principal.principal_id.empty())
      return deny(PolicyErrc::InvalidInput);
    m.rules(v.rules);
  } else if constexpr (std::is_same_v<T, TargetPolicyInput>) {
    if (v.target.empty() || !v.lifecycle_generation ||
        v.lifecycle_generation > s->budget.generation_limit)
      return deny(PolicyErrc::InvalidInput);
    if (!owned(v.lifetime_owner) ||
        dynamic_cast<ResourceLease *>(v.lifetime_owner.get()) ||
        dynamic_cast<ActivityLease *>(v.lifetime_owner.get()))
      return deny(PolicyErrc::InvalidOwner);
    m.rules(v.rules);
  } else {
    if constexpr (std::is_same_v<T, OperationPolicyInput>)
      m.selector(v.operation);
    else if (v.use <= AccessUse::Invoke || v.use > AccessUse::Subscribe)
      return deny(PolicyErrc::InvalidInput);
    m.count(v.required_permissions.size());
    if (!m.valid)
      return deny(PolicyErrc::BudgetExceeded);
    if (v.required_permissions.empty() || !unique(v.required_permissions))
      return deny(PolicyErrc::InvalidInput);
    for (auto &p : v.required_permissions)
      m.text(p.view());
    m.rules(v.module_rules);
  }
  return m.valid ? Result<void>{} : deny(PolicyErrc::BudgetExceeded);
}
template <class T, class Key>
Result<void> replace(std::vector<T> &v, const T &input, Key key) {
  for (auto &x : v)
    if (key(x) == key(input)) {
      x = input;
      return {};
    }
  v.push_back(input);
  return {};
}
} // namespace detail
Result<void>
PolicyAdministration::replace_principal_policy(const PrincipalPolicyInput &v) {
  auto checked = preflight_input(state_->value, v);
  if (!checked)
    return checked;
  return update_config(state_->value, [&](auto &c) {
    return replace(c.principals, v, [](auto &x) { return x.principal; });
  });
}
Result<void>
PolicyAdministration::replace_operation_policy(const OperationPolicyInput &v) {
  auto checked = preflight_input(state_->value, v);
  if (!checked)
    return checked;
  return update_config(state_->value, [&](auto &c) -> Result<void> {
    // 同一 Store 中精确 key 的合同身份终身固定，ACL 更新不重解释已有执行。
    for (const auto &installed : c.operations)
      if (installed.operation.operation == v.operation.operation &&
          installed.operation.contract != v.operation.contract)
        return deny(PolicyErrc::ContractMismatch);
    return replace(c.operations, v,
                   [](auto &x) { return x.operation.operation; });
  });
}
Result<void> PolicyAdministration::replace_use_policy(const UsePolicyInput &v) {
  auto checked = preflight_input(state_->value, v);
  if (!checked)
    return checked;
  return update_config(state_->value, [&](auto &c) {
    return replace(c.uses, v, [](auto &x) { return x.use; });
  });
}
Result<void>
PolicyAdministration::replace_target_policy(const TargetPolicyInput &v) {
  auto checked = preflight_input(state_->value, v);
  if (!checked)
    return checked;
  return update_config(
      state_->value,
      [&](auto &c) {
        return replace(c.targets, v, [](auto &x) { return x.target; });
      },
      v.target);
}
Result<void> PolicyAdministration::retire_target(ObjectId t) {
  return update_config(state_->value, [&](auto &c) -> Result<void> {
    auto found = std::find_if(c.targets.begin(), c.targets.end(),
                              [&](auto &x) { return x.target == t; });
    if (found == c.targets.end())
      return deny(PolicyErrc::TargetUnavailable);
    c.targets.erase(found);
    return {};
  });
}
Result<void> PolicyAdministration::set_lifecycle(ObjectId t,
                                                 std::uint64_t value) {
  return update_config(
      state_->value,
      [&](auto &c) -> Result<void> {
        for (auto &x : c.targets)
          if (x.target == t) {
            if (value <= x.lifecycle_generation)
              return deny(PolicyErrc::InvalidInput);
            x.lifecycle_generation = value;
            return {};
          }
        return deny(PolicyErrc::TargetUnavailable);
      },
      t);
}

namespace detail {
Result<void> entry_current(const Session &session, const Entry &e,
                           AccessUse use) {
  auto c = current(session);
  if (!c)
    return c;
  auto &s = *session.hold.store;
  auto &v = e.original.summary->value();
  for (auto &t : e.targets) {
    if (!stamp_current(s, t))
      return deny(PolicyErrc::TargetUnavailable);
    if (!allowed(session, use, e.operation, t.target, v.owner))
      return deny(PolicyErrc::Denied);
    for (auto f : e.fields)
      if (!allowed(session,
                   use == AccessUse::ListSummary ? use : AccessUse::GetSummary,
                   e.operation, t.target, v.owner, f))
        return deny(PolicyErrc::Denied);
  }
  return {};
}
struct ReadyEntry {
  Entry entry;
  std::shared_ptr<const ExecutionSummary> projection;
  std::uint64_t permission, delegation;
};
Usage entry_usage(const Entry &e, const PolicyBudget &b, bool &valid,
                  bool projection_owned = true) {
  Meter m{b};
  m.count(1 + e.original.actual_targets.size() + e.targets.size() +
          e.fields.size());
  m.selector(e.operation);
  if (projection_owned) {
    // Charge the new projection, not the shared immutable source summary.
    m.count(1 + (has(e.fields, SummaryField::Facts)
                     ? e.original.summary->value().facts.size()
                     : 0));
    m.selector(e.operation);
  }
  valid = m.valid;
  return m.value;
}
Result<ReadyEntry> prepare_entry(const std::shared_ptr<Session> &session,
                                 const ExecutionAccessInput &input,
                                 AccessUse use, bool projection_owned = true) {
  auto s = session->hold.store;
  if (!owned(input.summary) || input.actual_targets.empty() ||
      input.actual_targets.size() > s->budget.targets ||
      !unique(input.actual_targets))
    return failure<ReadyEntry>(PolicyErrc::TargetUnavailable);
  auto &v = input.summary->value();
  if (v.host != s->source_id.host) {
    std::shared_ptr<Frame> retired;
    {
      std::lock_guard lock(s->mutex);
      s->closed = true;
      retired = drain(*s);
    }
    return failure<ReadyEntry>(PolicyErrc::StoreClosed);
  }
  if (!validate_summary(v))
    return failure<ReadyEntry>(PolicyErrc::TargetUnavailable);
  std::shared_ptr<const PolicyConfiguration> config;
  {
    std::lock_guard lock(s->mutex);
    config = s->config;
  }
  auto op = std::find_if(
      config->operations.begin(), config->operations.end(),
      [&](auto &x) { return x.operation.operation == v.operation; });
  if (op == config->operations.end())
    return failure<ReadyEntry>(PolicyErrc::TargetUnavailable);
  Entry e{input, op->operation, std::vector<TargetStamp>(0),
          std::vector<SummaryField>(0)};
  e.targets.reserve(input.actual_targets.size());
  e.fields.reserve(6);
  std::uint64_t permission, delegation;
  {
    std::lock_guard lock(s->mutex);
    auto c = current(*session);
    if (!c)
      return make_unexpected(c.error());
    if (s->config != config)
      return failure<ReadyEntry>(PolicyErrc::Busy);
    permission = s->generation;
    delegation = session->generation;
    for (auto t : input.actual_targets) {
      auto value = stamp(*s, t);
      if (!value || !allowed(*session, use, e.operation, t, v.owner))
        return failure<ReadyEntry>(PolicyErrc::TargetUnavailable);
      e.targets.push_back(*value);
    }
    for (int f = 0; f <= static_cast<int>(SummaryField::Facts); ++f) {
      bool permitted = true;
      for (auto t : input.actual_targets)
        if (!allowed(*session,
                     use == AccessUse::ListSummary ? use
                                                   : AccessUse::GetSummary,
                     e.operation, t, v.owner, static_cast<SummaryField>(f)))
          permitted = false;
      if (permitted)
        e.fields.push_back(static_cast<SummaryField>(f));
    }
  }
  for (auto f :
       {SummaryField::Identity, SummaryField::Owner, SummaryField::Phase})
    if (!has(e.fields, f))
      return failure<ReadyEntry>(PolicyErrc::TargetUnavailable);
  bool within_budget;
  entry_usage(e, s->budget, within_budget, projection_owned);
  if (!within_budget)
    return failure<ReadyEntry>(PolicyErrc::BudgetExceeded);
  if (!projection_owned)
    return ReadyEntry{std::move(e), {}, permission, delegation};
  auto projected = v;
  if (!has(e.fields, SummaryField::Parent))
    projected.parent.reset();
  if (!has(e.fields, SummaryField::Progress))
    projected.progress = {};
  if (!has(e.fields, SummaryField::Facts))
    projected.facts.clear();
  auto frozen = ExecutionSummary::create(projected);
  if (!frozen)
    return make_unexpected(frozen.error());
  return ReadyEntry{std::move(e), *frozen, permission, delegation};
}
Result<void> response_current(const Response &r) {
  auto &session = *r.session;
  auto &s = *session.hold.store;
  auto c = current(session);
  if (!c)
    return c;
  if (r.source != s.source_id || r.permission != s.generation ||
      r.delegation != session.generation)
    return deny(PolicyErrc::Denied);
  if (s.clock->now() >= r.deadline)
    return deny(PolicyErrc::Expired);
  for (auto &e : r.entries) {
    c = entry_current(session, e, r.use);
    if (!c)
      return c;
  }
  return {};
}
Result<void> source_entries(const std::shared_ptr<Session> &session,
                            const std::vector<Entry> &entries) {
  auto s = session->hold.store;
  auto check = source_current(s);
  if (!check)
    return check;
  for (auto &e : entries) {
    auto actual = s->source->find(e.original.summary->value().execution);
    if (!actual || !owned(actual->summary))
      return deny(PolicyErrc::TargetUnavailable);
    auto &a = actual->summary->value();
    auto &b = e.original.summary->value();
    if (a.execution != b.execution || a.operation != b.operation ||
        a.owner != b.owner || a.host != b.host ||
        actual->actual_targets != e.original.actual_targets) {
      std::shared_ptr<Frame> retired;
      {
        std::lock_guard lock(s->mutex);
        s->closed = true;
        retired = drain(*s);
      }
      return deny(PolicyErrc::StoreClosed);
    }
  }
  return source_current(s);
}
} // namespace detail
Result<AuthorizedSummary>
ObservationAuthorization::get(const VerifiedCaller &caller,
                              ExecutionRef execution, AccessUse use) {
  try {
    auto session = state_->value->session;
    auto s = session->hold.store;
    if (Access::record(caller)->session != session)
      return failure<AuthorizedSummary>(PolicyErrc::InvalidAuthority);
    auto check = caller.view().revalidate();
    if (!check)
      return make_unexpected(check.error());
    if (use != AccessUse::GetSummary && use != AccessUse::Wait &&
        use != AccessUse::CancelExecution && use != AccessUse::ReadResult &&
        use != AccessUse::ReadLog && use != AccessUse::ReadAsset)
      return failure<AuthorizedSummary>(PolicyErrc::InvalidInput);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    auto input = s->source->find(execution);
    if (!input || !owned(input->summary) ||
        input->summary->value().execution != execution)
      return failure<AuthorizedSummary>(PolicyErrc::TargetUnavailable);
    auto ready = prepare_entry(session, *input, use);
    if (!ready)
      return make_unexpected(ready.error());
    auto projection =
        Access::make<ProjectionSnapshot>(std::make_shared<Projection>(
            Projection{ProjectionKind::Summary, ready->projection}));
    auto r = std::make_shared<Response>();
    r->hold.store = s;
    r->hold.kind = CountKind::Response;
    r->session = session;
    r->use = use;
    r->source = s->source_id;
    r->permission = ready->permission;
    r->delegation = ready->delegation;
    r->projection = projection;
    r->entries.push_back(std::move(ready->entry));
    bool valid;
    r->hold.usage = entry_usage(r->entries[0], s->budget, valid);
    if (!valid)
      return failure<AuthorizedSummary>(PolicyErrc::BudgetExceeded);
    auto ttl = expires(s->clock->now(), s->budget.queued_ttl);
    if (!ttl)
      return make_unexpected(ttl.error());
    r->deadline = (std::min)(*ttl, session->deadline);
    auto response = Access::make<ResponseAuthorization>(r);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    {
      std::lock_guard lock(s->mutex);
      check = response_current(*r);
      if (!check)
        return make_unexpected(check.error());
      auto id = next(*s);
      if (!id)
        return make_unexpected(id.error());
      r->id = identity<ResponseId>(s->serial, *id);
      check = acquire(r->hold);
      if (!check)
        return make_unexpected(check.error());
    }
    return AuthorizedSummary{ready->projection, response};
  } catch (const std::bad_alloc &) {
    return failure<AuthorizedSummary>(PolicyErrc::BudgetExceeded);
  }
}
Result<std::unique_ptr<SendCoordinator>>
SendCoordinator::create(std::shared_ptr<SessionAuthority> authority,
                        std::shared_ptr<TransmissionStartPort> sink,
                        std::shared_ptr<ProjectionEncoderPort> encoder) {
  try {
    if (!owned(authority) || !owned(sink) || !owned(encoder))
      return failure<std::unique_ptr<SendCoordinator>>(
          PolicyErrc::InvalidOwner);
    auto s = Access::record(*authority)->session;
    auto c = std::make_shared<Coordinator>();
    c->hold.store = s->hold.store;
    c->hold.usage.declarations = s->hold.store->budget.queued_frames;
    c->session = s;
    c->queues->session = s.get();
    c->sink = std::move(sink);
    c->encoder = std::move(encoder);
    c->queue.reserve(s->hold.store->budget.queued_frames);
    auto result = Access::make_unique<SendCoordinator>(c);
    {
      std::lock_guard lock(s->hold.store->mutex);
      auto check = current(*s);
      if (!check)
        return make_unexpected(check.error());
      if (s->hold.store->coordinators.size() >=
          s->hold.store->coordinators.capacity())
        return failure<std::unique_ptr<SendCoordinator>>(
            PolicyErrc::BudgetExceeded);
      check = acquire(c->hold);
      if (!check)
        return make_unexpected(check.error());
      s->hold.store->coordinators.push_back(c->queues);
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<std::unique_ptr<SendCoordinator>>(
        PolicyErrc::BudgetExceeded);
  }
}
Result<void> SendCoordinator::enqueue_response(
    std::shared_ptr<const ResponseAuthorization> response) {
  try {
    auto c = state_->value;
    auto s = c->session->hold.store;
    if (!owned(response))
      return deny(PolicyErrc::InvalidAuthority);
    auto r = Access::record(*response);
    if (r->session != c->session)
      return deny(PolicyErrc::InvalidAuthority);
    auto check = source_entries(c->session, r->entries);
    if (!check)
      return check;
    {
      std::lock_guard lock(s->mutex);
      check = response_current(*r);
      if (!check)
        return check;
      if (r->status != ResponseStatus::Fresh)
        return deny(PolicyErrc::InvalidInput);
    }
    auto bytes = c->encoder->encode(*r->projection, s->budget.frame_bytes);
    if (!bytes)
      return make_unexpected(bytes.error());
    if (bytes->empty() || bytes->size() > s->budget.frame_bytes)
      return deny(PolicyErrc::BudgetExceeded);
    auto f = std::make_shared<Frame>();
    f->hold.store = s;
    Meter meter{s->budget};
    for (const auto &entry : r->entries) {
      bool valid;
      const auto usage = entry_usage(entry, s->budget, valid, false);
      meter.count(usage.declarations);
      meter.valid =
          valid && charge(meter.value.text, usage.text, s->budget.text_bytes) &&
          meter.valid;
    }
    if (!meter.valid)
      return deny(PolicyErrc::BudgetExceeded);
    f->hold.usage = meter.value;
    f->response = response;
    f->projection = r->projection;
    f->entries = r->entries;
    f->deadline = r->deadline;
    f->permission = r->permission;
    f->delegation = r->delegation;
    f->transmission = Access::transmission(
        *bytes,
        TransmissionBinding{identity<StoreId>(s->serial, 0), c->session->id,
                            r->id, r->deadline},
        r->projection);
    auto reservation = c->sink->reserve(f->transmission->bytes().size());
    if (!reservation)
      return make_unexpected(reservation.error());
    if (!*reservation ||
        (*reservation)->capacity() < f->transmission->bytes().size())
      return deny(PolicyErrc::BudgetExceeded);
    f->reservation_owner = c->sink;
    f->reservation = std::move(*reservation);
    check = source_current(s);
    if (!check)
      return check;
    {
      std::lock_guard lock(s->mutex);
      check = response_current(*r);
      if (!check)
        return check;
      if (r->status != ResponseStatus::Fresh)
        return deny(PolicyErrc::InvalidInput);
      if (s->queued >= s->budget.queued_frames ||
          f->transmission->bytes().size() > s->budget.queued_bytes - s->bytes)
        return deny(PolicyErrc::BudgetExceeded);
      check = acquire(f->hold);
      if (!check)
        return check;
      c->queue.push_back(f);
      ++s->queued;
      s->bytes += f->transmission->bytes().size();
      f->charged = true;
      r->status = ResponseStatus::Queued;
    }
    return {};
  } catch (const std::bad_alloc &) {
    return deny(PolicyErrc::BudgetExceeded);
  }
}
Result<AuthorizedPage>
ObservationAuthorization::list(const VerifiedCaller &caller,
                               const ListRequest &request,
                               const std::optional<PageBinding> &continuation) {
  try {
    auto session = state_->value->session;
    auto s = session->hold.store;
    if (Access::record(caller)->session != session)
      return failure<AuthorizedPage>(PolicyErrc::InvalidAuthority);
    auto check = caller.view().revalidate();
    if (!check)
      return make_unexpected(check.error());
    if (request.owner.principal_id.empty() ||
        request.phases < PhaseSet::Nonterminal ||
        request.phases > PhaseSet::All || !request.budget.page_size ||
        request.budget.page_size > s->budget.page_size ||
        !request.budget.scan_limit ||
        request.budget.scan_limit > s->budget.scan_limit)
      return failure<AuthorizedPage>(PolicyErrc::InvalidInput);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    auto query = request;
    std::uint64_t permission, delegation;
    {
      std::lock_guard lock(s->mutex);
      check = current(*session);
      if (!check)
        return make_unexpected(check.error());
      permission = s->generation;
      delegation = session->generation;
      if (continuation) {
        auto &b = continuation->value();
        if (b.store != identity<StoreId>(s->serial, 0) ||
            b.connection != session->id ||
            b.delegation_generation != delegation ||
            b.permission_generation != permission || b.owner != request.owner ||
            b.phases != request.phases ||
            b.budget.page_size != request.budget.page_size ||
            b.budget.scan_limit != request.budget.scan_limit ||
            b.host != s->source_id.host || b.restore != s->source_id.restore)
          return failure<AuthorizedPage>(PolicyErrc::CursorInvalid);
        if (s->clock->now() >= b.deadline)
          return failure<AuthorizedPage>(PolicyErrc::Expired);
        if (request.position &&
            (request.position->host != b.host ||
             request.position->upper_ordinal != b.upper_ordinal ||
             request.position->before_ordinal != b.before_ordinal))
          return failure<AuthorizedPage>(PolicyErrc::CursorInvalid);
        query.position =
            KeysetPosition{b.host, b.upper_ordinal, b.before_ordinal};
      } else if (request.position)
        return failure<AuthorizedPage>(PolicyErrc::CursorInvalid);
      if (!owner_candidate(*session, AccessUse::ListSummary, request.owner))
        return failure<AuthorizedPage>(PolicyErrc::Denied);
    }
    auto scan = s->source->scan({query});
    if (!scan)
      return make_unexpected(scan.error());
    if (scan->host != s->source_id.host ||
        (scan->next_scan && scan->next_scan->host != s->source_id.host)) {
      std::lock_guard lock(s->mutex);
      s->closed = true;
      return failure<AuthorizedPage>(PolicyErrc::StoreClosed);
    }
    if (scan->candidates.size() > request.budget.scan_limit)
      return failure<AuthorizedPage>(PolicyErrc::InvalidInput);
    std::uint64_t last = query.position
                             ? query.position->before_ordinal
                             : (std::numeric_limits<std::uint64_t>::max)();
    auto upper =
        query.position
            ? query.position->upper_ordinal
            : (scan->candidates.empty() ? 0 : scan->candidates.front().first);
    for (auto &candidate : scan->candidates) {
      if (!candidate.first || candidate.first >= last ||
          candidate.first > upper)
        return failure<AuthorizedPage>(PolicyErrc::InvalidInput);
      last = candidate.first;
    }
    ListPage page{
        scan->host, std::vector<ListedSummary>(0), {}, scan->retention_scope};
    page.items.reserve(request.budget.page_size);
    auto response = std::make_shared<Response>();
    response->hold.store = s;
    response->hold.kind = CountKind::Response;
    response->session = session;
    response->source = s->source_id;
    response->use = AccessUse::ListSummary;
    response->permission = permission;
    response->delegation = delegation;
    response->entries.reserve(request.budget.page_size);
    std::uint64_t before = 0;
    std::size_t scanned = 0;
    for (auto &candidate : scan->candidates) {
      before = candidate.first;
      ++scanned;
      auto &input = candidate.second;
      if (!owned(input.summary))
        return failure<AuthorizedPage>(PolicyErrc::InvalidInput);
      auto &v = input.summary->value();
      if (v.owner != request.owner)
        continue;
      if ((request.phases == PhaseSet::Terminal &&
           v.phase != ExecutionPhase::Terminal) ||
          (request.phases == PhaseSet::Nonterminal &&
           v.phase == ExecutionPhase::Terminal))
        continue;
      auto ready = prepare_entry(session, input, AccessUse::ListSummary);
      if (!ready)
        continue;
      page.items.push_back({candidate.first, ready->projection});
      response->entries.push_back(std::move(ready->entry));
      if (page.items.size() == request.budget.page_size)
        break;
    }
    if (before && (scanned < scan->candidates.size() || scan->next_scan))
      page.next = KeysetPosition{s->source_id.host, upper, before};
    Meter meter{s->budget};
    meter.count(page.items.size());
    meter.text(page.retention_scope.view());
    for (auto &e : response->entries) {
      bool good;
      auto use = entry_usage(e, s->budget, good);
      if (!good)
        return failure<AuthorizedPage>(PolicyErrc::BudgetExceeded);
      meter.count(use.declarations);
      meter.valid = charge(meter.value.text, use.text, s->budget.text_bytes) &&
                    meter.valid;
    }
    if (!meter.valid)
      return failure<AuthorizedPage>(PolicyErrc::BudgetExceeded);
    response->hold.usage = meter.value;
    auto ttl = expires(s->clock->now(), s->budget.queued_ttl);
    if (!ttl)
      return make_unexpected(ttl.error());
    response->deadline = (std::min)(*ttl, session->deadline);
    auto projection = Access::make<ProjectionSnapshot>(
        std::make_shared<Projection>(Projection{ProjectionKind::Page, page}));
    response->projection = projection;
    auto result = Access::make<ResponseAuthorization>(response);
    std::optional<PageBinding> cursor;
    if (page.next) {
      auto expiry = expires(s->clock->now(), s->budget.page_ttl);
      if (!expiry)
        return make_unexpected(expiry.error());
      cursor =
          Access::page({identity<StoreId>(s->serial, 0), session->id,
                        delegation, permission, request.owner, request.phases,
                        request.budget, s->source_id.host, s->source_id.restore,
                        upper, before, (std::min)(*expiry, session->deadline)});
    }
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    {
      std::lock_guard lock(s->mutex);
      check = response_current(*response);
      if (!check)
        return make_unexpected(check.error());
      if (!owner_candidate(*session, AccessUse::ListSummary, request.owner))
        return failure<AuthorizedPage>(PolicyErrc::Denied);
      auto n = next(*s);
      if (!n)
        return make_unexpected(n.error());
      response->id = identity<ResponseId>(s->serial, *n);
      check = acquire(response->hold);
      if (!check)
        return make_unexpected(check.error());
    }
    return AuthorizedPage{std::move(page), std::move(cursor), result};
  } catch (const std::bad_alloc &) {
    return failure<AuthorizedPage>(PolicyErrc::BudgetExceeded);
  }
}
namespace detail {
Result<void> watch_current(const Watch &w) {
  auto &s = *w.session->hold.store;
  auto c = current(*w.session);
  if (!c)
    return c;
  if (w.closed)
    return deny(PolicyErrc::Denied);
  if (s.clock->now() >= w.deadline)
    return deny(PolicyErrc::Expired);
  if (w.source != s.source_id)
    return deny(PolicyErrc::StoreClosed);
  if (w.filter.owner &&
      !owner_candidate(*w.session, AccessUse::Subscribe, *w.filter.owner))
    return deny(PolicyErrc::Denied);
  for (auto &e : w.entries) {
    c = entry_current(*w.session, e, AccessUse::Subscribe);
    if (!c)
      return c;
  }
  return {};
}
} // namespace detail
Result<std::shared_ptr<WatchAuthorization>>
ObservationAuthorization::subscribe(const VerifiedCaller &caller,
                                    const ObservationFilter &input) {
  try {
    auto session = state_->value->session;
    auto s = session->hold.store;
    if (Access::record(caller)->session != session)
      return failure<std::shared_ptr<WatchAuthorization>>(
          PolicyErrc::InvalidAuthority);
    auto check = caller.view().revalidate();
    if (!check)
      return make_unexpected(check.error());
    if (input.executions.empty() == !input.owner ||
        input.executions.size() > s->budget.members || input.topics.empty() ||
        input.topics.size() > 3 || !unique(input.executions) ||
        !unique(input.topics))
      return failure<std::shared_ptr<WatchAuthorization>>(
          PolicyErrc::InvalidInput);
    for (auto topic : input.topics)
      if (topic < ObservationTopic::Progress || topic > ObservationTopic::Fact)
        return failure<std::shared_ptr<WatchAuthorization>>(
            PolicyErrc::InvalidInput);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    auto w = std::make_shared<Watch>();
    w->hold.store = s;
    w->hold.kind = CountKind::Watch;
    w->session = session;
    w->source = s->source_id;
    w->filter = input;
    w->deadline = session->deadline;
    w->entries.reserve(input.executions.size());
    Meter meter{s->budget};
    meter.count(input.executions.size() + input.topics.size() + 1);
    for (auto execution : input.executions) {
      auto raw = s->source->find(execution);
      if (!raw || !owned(raw->summary) ||
          raw->summary->value().execution != execution)
        return failure<std::shared_ptr<WatchAuthorization>>(
            PolicyErrc::TargetUnavailable);
      auto entry = prepare_entry(session, *raw, AccessUse::Subscribe, false);
      if (!entry)
        return make_unexpected(entry.error());
      bool valid;
      auto usage = entry_usage(entry->entry, s->budget, valid, false);
      meter.count(usage.declarations);
      meter.valid =
          valid && charge(meter.value.text, usage.text, s->budget.text_bytes) &&
          meter.valid;
      w->entries.push_back(std::move(entry->entry));
    }
    if (!meter.valid)
      return failure<std::shared_ptr<WatchAuthorization>>(
          PolicyErrc::BudgetExceeded);
    w->hold.usage = meter.value;
    auto result = Access::make<WatchAuthorization>(w);
    check = source_current(s);
    if (!check)
      return make_unexpected(check.error());
    {
      std::lock_guard lock(s->mutex);
      check = watch_current(*w);
      if (!check)
        return make_unexpected(check.error());
      if (session->watches >= s->budget.watches_per_session)
        return failure<std::shared_ptr<WatchAuthorization>>(
            PolicyErrc::BudgetExceeded);
      auto count =
          std::find_if(s->watch_counts.begin(), s->watch_counts.end(),
                       [&](auto &i) { return i.first == session->principal; });
      if (count != s->watch_counts.end() &&
          count->second >= s->budget.watches_per_principal)
        return failure<std::shared_ptr<WatchAuthorization>>(
            PolicyErrc::BudgetExceeded);
      if (count == s->watch_counts.end() &&
          s->watch_counts.size() >= s->watch_counts.capacity())
        return failure<std::shared_ptr<WatchAuthorization>>(
            PolicyErrc::BudgetExceeded);
      auto n = next(*s);
      if (!n)
        return make_unexpected(n.error());
      w->key = identity<WatchKey>(s->serial, *n);
      check = acquire(w->hold);
      if (!check)
        return make_unexpected(check.error());
      ++session->watches;
      if (count == s->watch_counts.end())
        s->watch_counts.push_back({session->principal, 1});
      else
        ++count->second;
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<std::shared_ptr<WatchAuthorization>>(
        PolicyErrc::BudgetExceeded);
  }
}

Result<void> SendCoordinator::enqueue(const WatchAuthorization &authorization,
                                      const ChangeHint &hint) {
  try {
    auto c = state_->value;
    auto s = c->session->hold.store;
    auto w = Access::record(authorization);
    if (w->session != c->session || !owned(hint.summary) ||
        !has(w->filter.topics, hint.topic))
      return deny(PolicyErrc::InvalidAuthority);
    auto execution = hint.summary->value().execution;
    if (!w->filter.owner && !has(w->filter.executions, execution))
      return deny(PolicyErrc::Denied);
    auto check = source_entries(c->session, w->entries);
    if (!check)
      return check;
    auto actual = s->source->find(execution);
    if (!actual || !owned(actual->summary) ||
        actual->summary->value().execution != execution)
      return deny(PolicyErrc::TargetUnavailable);
    if (w->filter.owner && actual->summary->value().owner != *w->filter.owner)
      return deny(PolicyErrc::Denied);
    auto ready = prepare_entry(c->session, *actual, AccessUse::Subscribe);
    if (!ready)
      return make_unexpected(ready.error());
    auto required =
        hint.topic == ObservationTopic::Progress ? SummaryField::Progress
        : hint.topic == ObservationTopic::Phase  ? SummaryField::Phase
                                                 : SummaryField::Facts;
    if (!has(ready->entry.fields, required))
      return deny(PolicyErrc::Denied);
    auto f = std::make_shared<Frame>();
    f->hold.store = s;
    bool valid;
    f->hold.usage = entry_usage(ready->entry, s->budget, valid);
    if (!valid)
      return deny(PolicyErrc::BudgetExceeded);
    f->watch = w;
    f->entries.push_back(std::move(ready->entry));
    f->permission = ready->permission;
    f->delegation = ready->delegation;
    auto expiry = expires(s->clock->now(), s->budget.queued_ttl);
    if (!expiry)
      return make_unexpected(expiry.error());
    f->deadline = (std::min)(*expiry, w->deadline);
    f->projection =
        Access::make<ProjectionSnapshot>(std::make_shared<Projection>(
            Projection{ProjectionKind::Hint,
                       ChangeHint{ready->projection, hint.topic, hint.gap}}));
    {
      std::lock_guard lock(s->mutex);
      check = watch_current(*w);
      if (!check)
        return check;
    }
    auto bytes = c->encoder->encode(*f->projection, s->budget.frame_bytes);
    if (!bytes)
      return make_unexpected(bytes.error());
    if (bytes->empty() || bytes->size() > s->budget.frame_bytes)
      return deny(PolicyErrc::BudgetExceeded);
    f->transmission =
        Access::transmission(*bytes,
                             {identity<StoreId>(s->serial, 0), c->session->id,
                              WatchStamp{w->key, w->generation}, f->deadline},
                             f->projection);
    auto reservation = c->sink->reserve(f->transmission->bytes().size());
    if (!reservation)
      return make_unexpected(reservation.error());
    if (!*reservation ||
        (*reservation)->capacity() < f->transmission->bytes().size())
      return deny(PolicyErrc::BudgetExceeded);
    f->reservation_owner = c->sink;
    f->reservation = std::move(*reservation);
    check = source_current(s);
    if (!check)
      return check;
    {
      std::lock_guard lock(s->mutex);
      check = watch_current(*w);
      if (!check)
        return check;
      if (s->generation != f->permission ||
          c->session->generation != f->delegation ||
          s->clock->now() >= f->deadline)
        return deny(PolicyErrc::Denied);
      check = entry_current(*c->session, f->entries[0], AccessUse::Subscribe);
      if (!check)
        return check;
      if (s->queued >= s->budget.queued_frames ||
          f->transmission->bytes().size() > s->budget.queued_bytes - s->bytes)
        return deny(PolicyErrc::BudgetExceeded);
      check = acquire(f->hold);
      if (!check)
        return check;
      c->queue.push_back(f);
      ++s->queued;
      s->bytes += f->transmission->bytes().size();
      f->charged = true;
    }
    return {};
  } catch (const std::bad_alloc &) {
    return deny(PolicyErrc::BudgetExceeded);
  }
}
Result<bool>
SendCoordinator::unsubscribe(const WatchAuthorization &authorization) {
  auto c = state_->value;
  auto s = c->session->hold.store;
  auto w = Access::record(authorization);
  std::shared_ptr<Frame> retired;
  {
    std::lock_guard lock(s->mutex);
    if (s->closed)
      return failure<bool>(PolicyErrc::StoreClosed);
    if (w->session != c->session || w->closed || c->session->closed)
      return false;
    w->closed = true;
    retired = drain(*s, c->session.get(), w.get());
  }
  return true;
}
Result<StartResult> SendCoordinator::start_next() {
  try {
    auto c = state_->value;
    std::unique_lock pump(c->pumping, std::try_to_lock);
    if (!pump.owns_lock())
      return failure<StartResult>(PolicyErrc::Busy);
    auto s = c->session->hold.store;
    std::shared_ptr<Frame> f;
    {
      std::lock_guard lock(s->mutex);
      auto check = current(*c->session);
      if (!check && c->queue.empty())
        return make_unexpected(check.error());
      if (c->queue.empty())
        return StartResult::NotStarted;
      f = c->queue.front();
    }
    auto source = source_entries(c->session, f->entries);
    Result<StartResult> result = StartResult::NotStarted;
    std::shared_ptr<Frame> retired;
    bool unknown = false;
    std::shared_ptr<Response> r;
    if (f->response)
      r = Access::record(*f->response);
    {
      std::lock_guard lock(s->mutex);
      if (!f->charged)
        return failure<StartResult>(s->closed ? PolicyErrc::StoreClosed
                                              : PolicyErrc::Denied);
      auto check = r ? response_current(*r) : watch_current(*f->watch);
      if (!source)
        check = make_unexpected(source.error());
      if (check && (s->generation != f->permission ||
                    c->session->generation != f->delegation ||
                    s->clock->now() >= f->deadline))
        check = deny(PolicyErrc::Denied);
      if (check)
        for (auto &e : f->entries) {
          check =
              entry_current(*c->session, e, r ? r->use : AccessUse::Subscribe);
          if (!check)
            break;
        }
      if (!check) {
        if (r)
          r->status = ResponseStatus::Dropped;
        result = make_unexpected(check.error());
      } else {
        auto started = c->sink->start_now(*f->transmission, *f->reservation);
        result = started;
        if (started == StartResult::NotStarted)
          return result;
        if (r)
          r->status = started == StartResult::Started ? ResponseStatus::Started
                                                      : ResponseStatus::Unknown;
        if (started == StartResult::Unknown) {
          c->session->closed = true;
          unknown = true;
        }
      }
      if (f->charged) {
        --s->queued;
        s->bytes -= f->transmission->bytes().size();
        f->charged = false;
      }
      c->queue.erase(c->queue.begin());
      if (unknown)
        retired = drain(*s, c->session.get());
    }
    return result;
  } catch (const std::bad_alloc &) {
    return failure<StartResult>(PolicyErrc::BudgetExceeded);
  }
}
} // namespace ock::runtime::policy
