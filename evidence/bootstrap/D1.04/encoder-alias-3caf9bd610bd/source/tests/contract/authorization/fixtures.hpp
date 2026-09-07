#pragma once
#include "packages/runtime/policy/policy.hpp"
#include "tests/compile/contracts/test_support.hpp"
namespace policy_test {
using namespace ock::runtime::policy;
inline void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Clock final : ClockPort {
  TimePoint base = std::chrono::steady_clock::now();
  std::atomic<std::int64_t> elapsed{0};
  TimePoint now() const noexcept override {
    return base + std::chrono::milliseconds(elapsed.load());
  }
};
inline PrincipalRef principal(unsigned n = 1) { return {id<PrincipalId>(n)}; }
inline ObjectId target(unsigned n = 1) { return id<ObjectId>(n); }
inline OperationSelector operation(unsigned n = 1) {
  auto k = key();
  if (n != 1)
    k.name = name("test.second");
  return {k, {}};
}
inline std::vector<ScopeRule> rules() {
  std::vector<ScopeRule> result;
  for (int u = 0; u <= static_cast<int>(AccessUse::Subscribe); ++u) {
    auto use = static_cast<AccessUse>(u);
    for (unsigned op = 1; op <= 2; ++op)
      result.push_back(
          {use,
           operation(op),
           {name("allow")},
           {target(), target(2)},
           {principal(), principal(2)},
           {SummaryField::Identity, SummaryField::Owner, SummaryField::Parent,
            SummaryField::Phase, SummaryField::Progress, SummaryField::Facts}});
  }
  return result;
}
struct Lifetime final : PortLifetime {};
inline PolicyConfiguration configuration() {
  PolicyConfiguration c;
  c.principals = {{principal(), rules()}, {principal(2), rules()}};
  c.operations = {{operation(), {name("allow")}, rules(), true},
                  {operation(2), {name("allow")}, rules(), false}};
  for (int u = 1; u <= static_cast<int>(AccessUse::Subscribe); ++u)
    c.uses.push_back({static_cast<AccessUse>(u), {name("allow")}, rules()});
  c.targets = {{target(), 1, rules(), std::make_shared<Lifetime>()},
               {target(2), 1, rules(), std::make_shared<Lifetime>()}};
  return c;
}
struct Auth final : TrustedAuthenticationPort {
  AuthenticatedIdentity identity;
  explicit Auth(TimePoint deadline)
      : identity{principal(),
                 PrincipalKind::User,
                 {},
                 {rules(), deadline, false},
                 deadline} {}
  Result<AuthenticatedIdentity>
  authenticate(const AuthenticationAttempt &a) override {
    if (a.credential != std::vector<std::byte>{std::byte{7}})
      return make_unexpected(policy_error(PolicyErrc::AuthenticationFailed));
    return identity;
  }
};
struct Digest final : TrustedGroupDigestPort {
  bool collision = false, throw_allocation = false;
  Result<ContractDigest> fingerprint(const GroupSnapshot &g) override {
    if (throw_allocation)
      throw std::bad_alloc();
    ContractDigest d{};
    std::uint64_t value = 1469598103934665603ULL;
    auto byte = [&](unsigned char b) {
      value = (value ^ b) * 1099511628211ULL;
    };
    auto count = [&](std::uint64_t n) {
      for (unsigned i = 0; i < 8; ++i)
        byte(static_cast<unsigned char>(n >> (8 * i)));
    };
    auto chars = [&](std::string_view text) {
      count(text.size());
      for (unsigned char c : text)
        byte(c);
    };
    auto selector = [&](const OperationSelector &o) {
      chars(o.operation.name.view());
      chars(o.operation.version.text());
      for (auto b : o.contract.bytes)
        byte(std::to_integer<unsigned char>(b));
    };
    byte(1);
    selector(g.envelope());
    for (auto b : g.anchor_target().bytes)
      byte(b);
    count(g.members().size());
    for (auto &m : g.members()) {
      byte(2);
      selector(m.operation);
      count(m.targets.size());
      for (auto t : m.targets)
        for (auto b : t.bytes)
          byte(b);
    }
    if (!collision)
      for (unsigned i = 0; i < 8; ++i)
        d.bytes[i] = std::byte(value >> (8 * i));
    return d;
  }
};
struct Source final : ExecutionAccessSourcePort {
  ObservationSourceIdentity source_id{id<HostIncarnation>(),
                                      RestoreMode::Absent};
  std::vector<std::pair<std::uint64_t, ExecutionAccessInput>> rows;
  unsigned scans = 0;
  bool find_failure = false, scan_failure = false;
  Source() {
    for (unsigned n = 1; n <= 2; ++n) {
      SummaryInput s{{id<foundation::TaskId>(n)},
                     operation().operation,
                     principal(),
                     {},
                     ExecutionPhase::Running,
                     source_id.host,
                     *ObservationVersion::create(1),
                     {},
                     {}};
      auto made = ExecutionSummary::create(s);
      CHECK(made);
      rows.push_back({3 - n, {*made, {target(n)}}});
    }
  }
  ObservationSourceIdentity identity() const noexcept override {
    return source_id;
  }
  Result<ExecutionAccessInput> find(ExecutionRef e) override {
    if (find_failure)
      throw std::bad_alloc();
    for (auto &r : rows)
      if (r.second.summary->value().execution == e)
        return r.second;
    return make_unexpected(policy_error(PolicyErrc::TargetUnavailable));
  }
  Result<AccessScanPage> scan(const AccessScanRequest &r) override {
    ++scans;
    if (scan_failure)
      throw std::bad_alloc();
    AccessScanPage p{{}, {}, source_id.host, name("retained")};
    std::size_t seen = 0;
    for (auto &row : rows) {
      if (r.list.position && (row.first >= r.list.position->before_ordinal ||
                              row.first > r.list.position->upper_ordinal))
        continue;
      if (seen++ >= r.list.budget.scan_limit)
        break;
      p.candidates.push_back(row);
    }
    if (!p.candidates.empty() && p.candidates.back().first > 1)
      p.next_scan = KeysetPosition{
          source_id.host,
          r.list.position ? r.list.position->upper_ordinal : rows.front().first,
          p.candidates.back().first};
    return p;
  }
};
struct Encoder final : ProjectionEncoderPort {
  bool throw_allocation = false;
  Result<std::vector<std::byte>> encode(const ProjectionSnapshot &p,
                                        std::size_t cap) override {
    if (throw_allocation)
      throw std::bad_alloc();
    if (cap < 2)
      return make_unexpected(policy_error(PolicyErrc::BudgetExceeded));
    return std::vector<std::byte>{std::byte{0x51},
                                  std::byte(static_cast<unsigned>(p.kind()))};
  }
};
struct Sink final : TransmissionStartPort {
  struct Reservation final : TransmissionReservation {
    Sink *owner;
    std::size_t count;
    bool used = false;
    ~Reservation() {
      if (!used)
        owner->pending -= count;
    }
    Reservation(Sink *p, std::size_t n) : owner(p), count(n) {}
    std::size_t capacity() const noexcept override { return count; }
  };
  StoreId last_store{};
  std::array<std::byte, 32768> data{};
  std::size_t size = 0, pending = 0;
  unsigned starts = 0;
  StartResult result = StartResult::Started;
  bool reject_reserve = false, throw_allocation = false;
  std::function<void()> before_reserve_return;
  Result<std::unique_ptr<TransmissionReservation>>
  reserve(std::size_t n) override {
    if (throw_allocation)
      throw std::bad_alloc();
    if (reject_reserve || size > data.size() || pending > data.size() - size ||
        n > data.size() - size - pending)
      return make_unexpected(policy_error(PolicyErrc::Busy));
    auto reservation = std::make_unique<Reservation>(this, n);
    pending += n;
    if (before_reserve_return)
      before_reserve_return();
    return std::unique_ptr<TransmissionReservation>(std::move(reservation));
  }
  StartResult start_now(const PreparedTransmission &frame,
                        TransmissionReservation &r) noexcept override {
    auto *own = dynamic_cast<Reservation *>(&r);
    if (!own || own->owner != this || own->used ||
        own->count < frame.bytes().size())
      return StartResult::NotStarted;
    ++starts;
    if (result == StartResult::NotStarted)
      return result;
    if (frame.bytes().size() > data.size() - size)
      return StartResult::NotStarted;
    own->used = true;
    pending -= own->count;
    last_store = frame.binding().store;
    for (auto b : frame.bytes())
      data[size++] = b;
    return result;
  }
};
struct Env {
  std::shared_ptr<Clock> clock = std::make_shared<Clock>();
  std::shared_ptr<Auth> auth =
      std::make_shared<Auth>(clock->now() + std::chrono::hours(1));
  std::shared_ptr<Digest> digest = std::make_shared<Digest>();
  std::shared_ptr<Source> source = std::make_shared<Source>();
  PolicyAssembly assembly;
  std::shared_ptr<SessionAuthority> session;
  std::shared_ptr<const VerifiedCaller> caller;
  explicit Env(PolicyBudget budget = {},
               PolicyConfiguration cfg = configuration()) {
    auto a = PolicyStore::create(budget, cfg, auth, clock, digest, source);
    CHECK(a);
    assembly = std::move(*a);
    auto s = assembly.store->open({{std::byte{7}}},
                                  {rules(), auth->identity.deadline, false});
    CHECK(s);
    session = *s;
    auto c = session->verify({principal(), {}, {}});
    CHECK(c);
    caller = *c;
  }
  ActionRequest request() {
    return {operation(),
            target(),
            {{operation(), {target()}}},
            clock->now() + std::chrono::seconds(20)};
  }
  Result<std::shared_ptr<ActionAuthorization>> prepare() {
    return session->prepare(*caller, request());
  }
};
} // namespace policy_test
