#include <atomic>
#include <mutex>
#include <ock/control/subscription.hpp>
namespace ock::control {
namespace policy = runtime::policy;
namespace {
template <class T> Result<T> fail() {
  return foundation::make_unexpected(error(ProtocolErrc::BudgetExceeded));
}
void text(data::PayloadBuilder &b, std::string_view key,
          std::string_view value) {
  (void)b.key(key);
  (void)b.string(value);
}
template <class Id>
void identity(data::PayloadBuilder &b, std::string_view key, const Id &value) {
  text(b, key, wire_text(value));
}
void count(data::PayloadBuilder &b, std::string_view key, std::uint64_t value) {
  text(b, key, std::to_string(value));
}
std::string_view topic(contracts::ObservationTopic value) {
  switch (value) {
  case contracts::ObservationTopic::Progress:
    return "execution.progress";
  case contracts::ObservationTopic::Phase:
    return "execution.phase";
  case contracts::ObservationTopic::Fact:
    return "execution.fact";
  }
  return {};
}
} // namespace
struct SubscriptionConnection::State : std::enable_shared_from_this<State> {
  struct Receiver;
  struct Entry {
    UnsubscribeRequest token;
    std::shared_ptr<policy::WatchAuthorization> watch;
    std::unique_ptr<contracts::ObservationLease> lease;
    std::shared_ptr<Receiver> receiver;
    std::atomic<bool> active{true}, ack{false}, progress_pending{false};
    std::atomic<std::size_t> pending{0};
    std::uint64_t sequence = 1;
    bool gap = false;
    std::chrono::milliseconds interval{100};
    std::optional<policy::TimePoint> last_progress;
  };
  std::recursive_mutex mutex;
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::shared_ptr<contracts::ObservationPort> source;
  std::shared_ptr<ObservationTransport> transport;
  std::shared_ptr<policy::ClockPort> clock;
  contracts::HostIncarnation host;
  SubscriptionBudget budget;
  std::unique_ptr<policy::SendCoordinator> sender;
  std::vector<std::shared_ptr<Entry>> entries;
  std::atomic<std::size_t> bytes{0};
  bool closed = false;
  std::optional<policy::TimePoint> slow_since;
  std::shared_ptr<Entry> current;
  bool current_progress = false;
  struct Receiver final : contracts::ObservationReceiver {
    std::weak_ptr<State> owner;
    std::weak_ptr<Entry> entry;
    Receiver(std::shared_ptr<State> s, std::shared_ptr<Entry> e)
        : owner(s), entry(e) {}
    void changed(contracts::ChangeHint hint) noexcept override {
      if (auto s = owner.lock())
        if (auto e = entry.lock())
          try {
            s->enqueue(e, hint);
          } catch (...) {
            e->active = false;
          }
    }
  };
  void enqueue(const std::shared_ptr<Entry> &entry,
               const contracts::ChangeHint &hint) {
    std::lock_guard lock(mutex);
    if (closed || !entry->active)
      return;
    struct Restore {
      State &state;
      std::shared_ptr<Entry> entry;
      bool progress;
      ~Restore() {
        state.current = std::move(entry);
        state.current_progress = progress;
      }
    } restore{*this, current, current_progress};
    current = entry;
    const auto before = entry->sequence;
    auto result = sender->enqueue(*entry->watch, hint);
    if (!result && entry->sequence != before)
      entry->gap = true;
  }
  struct Encoder final : policy::ProjectionEncoderPort {
    std::weak_ptr<State> owner;
    explicit Encoder(std::shared_ptr<State> s) : owner(s) {}
    Result<std::vector<std::byte>>
    encode(const policy::ProjectionSnapshot &projection,
           std::size_t maximum) override {
      auto s = owner.lock();
      if (!s || !s->current ||
          projection.kind() != policy::ProjectionKind::Hint)
        return fail<std::vector<std::byte>>();
      auto e = s->current;
      auto &hint = std::get<contracts::ChangeHint>(projection.value());
      const auto &summary = hint.summary->value();
      if (summary.host != s->host || !e->sequence) {
        e->active = false;
        return fail<std::vector<std::byte>>();
      }
      // 此回调由 SendCoordinator 在初步授权/投影后调用，序号不计入隐藏事件。
      const auto sequence = e->sequence;
      e->sequence = sequence == s->budget.sequence_limit ? 0 : sequence + 1;
      s->current_progress = hint.topic == contracts::ObservationTopic::Progress;
      if (s->current_progress &&
          (e->progress_pending ||
           (e->last_progress &&
            s->clock->now() - *e->last_progress < e->interval))) {
        e->gap = true;
        return fail<std::vector<std::byte>>();
      }
      const contracts::FactSummary *fact = nullptr;
      if (hint.topic == contracts::ObservationTopic::Fact) {
        // 一条有损提示只携带一个最小摘要；多事实合并必须引导客户端重查。
        if (summary.facts.size() > 1)
          e->gap = true;
        for (auto it = summary.facts.rbegin(); it != summary.facts.rend(); ++it) {
          if (it->reference &&
              (it->kind == contracts::FactKind::Published ||
               it->kind == contracts::FactKind::Effect ||
               it->kind == contracts::FactKind::Lifecycle)) {
            fact = &*it;
            break;
          }
        }
        if (!fact) {
          e->gap = true;
          return fail<std::vector<std::byte>>();
        }
      }
      data::PayloadBuilder b;
      (void)b.begin_object();
      text(b, "jsonrpc", "2.0");
      text(b, "method", "notifications.event");
      (void)b.key("params");
      (void)b.begin_object();
      identity(b, "subscription_id", e->token.subscription);
      identity(b, "stream_generation", e->token.stream);
      identity(b, "host_incarnation", s->host);
      count(b, "sequence", sequence);
      text(b, "topic", topic(hint.topic));
      (void)b.key("execution_ref");
      (void)b.begin_object();
      identity(b, "execution_id", summary.execution.execution_id);
      (void)b.end_object();
      count(b, "observation_version", summary.version.value());
      (void)b.key("gap");
      (void)b.boolean(e->gap || hint.gap);
      (void)b.key("data");
      (void)b.begin_object();
      if (hint.topic == contracts::ObservationTopic::Progress) {
        count(b, "completed", summary.progress.completed);
        count(b, "total", summary.progress.total);
        (void)b.key("published");
        (void)b.boolean(false);
      } else if (hint.topic == contracts::ObservationTopic::Phase) {
        constexpr std::string_view names[] = {
            "Queued",     "WaitingResources", "Running", "WaitingChild",
            "Finalizing", "Suspended",        "Terminal"};
        text(b, "phase", names[static_cast<unsigned>(summary.phase)]);
      } else {
        text(b, "kind", fact->kind == contracts::FactKind::Published
                            ? "StateCommitted"
                            : fact->kind == contracts::FactKind::Effect
                                  ? "EffectResolved" : "LifecycleResolved");
        std::visit([&](const auto &ref) { identity(b, "reference", ref); },
                   *fact->reference);
        (void)b.key("published");
        (void)b.boolean(fact->kind == contracts::FactKind::Published);
      }
      (void)b.end_object();
      (void)b.end_object();
      (void)b.end_object();
      auto value = b.freeze();
      if (!value)
        return foundation::make_unexpected(value.error());
      maximum = (std::min)(maximum, s->budget.frame_bytes);
      if (maximum <= 12)
        return fail<std::vector<std::byte>>();
      auto encoded = encode_frame(*value, maximum - 12);
      if (encoded)
        e->gap = false;
      return encoded;
    }
  };
  struct Reservation final : policy::TransmissionReservation {
    std::weak_ptr<State> owner;
    std::shared_ptr<Entry> entry;
    std::unique_ptr<policy::TransmissionReservation> inner;
    std::size_t size;
    bool progress;
    Reservation(std::shared_ptr<State> s, std::shared_ptr<Entry> e,
                std::unique_ptr<policy::TransmissionReservation> i,
                std::size_t n, bool p)
        : owner(s), entry(std::move(e)), inner(std::move(i)), size(n),
          progress(p) {
      s->bytes += n;
      ++entry->pending;
      if (progress)
        entry->progress_pending = true;
    }
    ~Reservation() {
      if (auto s = owner.lock())
        s->bytes -= size;
      --entry->pending;
      if (progress)
        entry->progress_pending = false;
    }
    std::size_t capacity() const noexcept override { return inner->capacity(); }
  };
  struct Sink final : policy::TransmissionStartPort {
    std::weak_ptr<State> owner;
    explicit Sink(std::shared_ptr<State> s) : owner(s) {}
    Result<std::unique_ptr<policy::TransmissionReservation>>
    reserve(std::size_t n) override {
      auto s = owner.lock();
      if (!s || !s->current ||
          s->current->pending >= s->budget.pending_per_subscription ||
          n > s->budget.queued_bytes - s->bytes.load())
        return fail<std::unique_ptr<policy::TransmissionReservation>>();
      auto entry = s->current;
      const auto progress = s->current_progress;
      auto inner = s->transport->reserve(n);
      if (!inner)
        return foundation::make_unexpected(inner.error());
      if (!*inner || (*inner)->capacity() < n)
        return fail<std::unique_ptr<policy::TransmissionReservation>>();
      // reserve 可重入；外部调用返回后重新核对仍有效的配额与订阅。
      if (s->closed || !entry->active ||
          entry->pending >= s->budget.pending_per_subscription ||
          n > s->budget.queued_bytes - s->bytes.load())
        return fail<std::unique_ptr<policy::TransmissionReservation>>();
      return std::unique_ptr<policy::TransmissionReservation>(
          std::make_unique<Reservation>(s, entry, std::move(*inner), n,
                                        progress));
    }
    policy::StartResult
    start_now(const policy::PreparedTransmission &frame,
              policy::TransmissionReservation &token) noexcept override {
      auto s = owner.lock();
      auto *reservation = dynamic_cast<Reservation *>(&token);
      if (!s || !reservation || !reservation->entry->ack)
        return policy::StartResult::NotStarted;
      auto result = s->transport->start_now(frame, *reservation->inner);
      if (result == policy::StartResult::Started && reservation->progress)
        reservation->entry->last_progress = s->clock->now();
      return result;
    }
  };
};
Result<std::unique_ptr<SubscriptionConnection>> SubscriptionConnection::create(
    std::shared_ptr<policy::SessionAuthority> session,
    std::shared_ptr<const policy::VerifiedCaller> caller,
    std::shared_ptr<contracts::ObservationPort> source,
    std::shared_ptr<ObservationTransport> transport,
    std::shared_ptr<policy::ClockPort> clock, contracts::HostIncarnation host,
    SubscriptionBudget budget) {
  if (!session || !caller || !source || !transport || !clock || host.empty() ||
      !budget.subscriptions || budget.subscriptions > 8 ||
      !budget.pending_per_subscription ||
      budget.pending_per_subscription > 128 || !budget.queued_bytes ||
      budget.queued_bytes > 1024 * 1024 || budget.frame_bytes < 1024 ||
      budget.frame_bytes > 16384 || !budget.sequence_limit ||
      budget.transport_timeout.count() <= 0 ||
      budget.transport_timeout.count() > 30000)
    return fail<std::unique_ptr<SubscriptionConnection>>();
  const auto &limits = session->limits();
  // 复用既有共享 Host 队列上限作为更严格的主体上界，不另建通用配额系统。
  if (limits.watches_per_principal > 32 || limits.active_watches > 256 ||
      limits.queued_bytes > 4 * 1024 * 1024)
    return fail<std::unique_ptr<SubscriptionConnection>>();
  budget.frame_bytes = (std::min)(budget.frame_bytes, limits.frame_bytes);
  budget.queued_bytes = (std::min)(budget.queued_bytes, limits.queued_bytes);
  if (!caller->view().belongs_to(*session->callers()) ||
      !caller->view().revalidate())
    return fail<std::unique_ptr<SubscriptionConnection>>();
  auto s = std::make_shared<State>();
  s->session = std::move(session);
  s->caller = std::move(caller);
  s->source = std::move(source);
  s->transport = std::move(transport);
  s->clock = std::move(clock);
  s->host = host;
  s->budget = budget;
  auto sender = policy::SendCoordinator::create(
      s->session, std::make_shared<State::Sink>(s),
      std::make_shared<State::Encoder>(s));
  if (!sender)
    return foundation::make_unexpected(sender.error());
  s->sender = std::move(*sender);
  return std::unique_ptr<SubscriptionConnection>(
      new SubscriptionConnection(std::move(s)));
}
SubscriptionConnection::~SubscriptionConnection() { close(); }
Result<UnsubscribeRequest>
SubscriptionConnection::subscribe(std::string_view request_id,
                                  const SubscribeRequest &request) {
  if (request_id.empty() || request_id.size() > 96 ||
      std::any_of(request_id.begin(), request_id.end(),
                  [](unsigned char c) { return c < 33 || c > 126; }) ||
      !contracts::validate_observation_filter(request.filter, 32) ||
      request.interval.count() < 0 || request.interval.count() > INT32_MAX)
    return fail<UnsubscribeRequest>();
  auto s = state_;
  auto e = std::make_shared<State::Entry>();
  {
    std::lock_guard lock(s->mutex);
    if (s->closed || s->entries.size() >= s->budget.subscriptions)
      return fail<UnsubscribeRequest>();
    auto watch =
        s->session->observations()->subscribe(*s->caller, request.filter);
    if (!watch)
      return foundation::make_unexpected(watch.error());
    e->watch = *watch;
    e->token.subscription.bytes = e->watch->key().bytes;
    e->token.stream.bytes = e->watch->key().bytes;
    e->interval = (std::max)(request.interval, std::chrono::milliseconds(50));
    e->receiver = std::make_shared<State::Receiver>(s, e);
    s->entries.push_back(e);
  }
  struct Rollback {
    SubscriptionConnection &connection;
    UnsubscribeRequest token;
    bool committed = false;
    ~Rollback() {
      if (!committed)
        (void)connection.unsubscribe(token);
    }
  } rollback{*this, e->token};
  // 注册端口可以同步发出提示；此时 Sink 的 pending-ack 门禁止发送。
  auto lease = s->source->observe_changes(s->caller->view(), e->watch->filter(),
                                          e->receiver);
  if (!lease || !*lease) {
    (void)unsubscribe(e->token);
    return fail<UnsubscribeRequest>();
  }
  {
    std::lock_guard lock(s->mutex);
    if (!e->active) {
      return fail<UnsubscribeRequest>();
    }
    e->lease = std::move(*lease);
  }
  data::PayloadBuilder b;
  (void)b.begin_object();
  text(b, "jsonrpc", "2.0");
  text(b, "id", request_id);
  (void)b.key("result");
  (void)b.begin_object();
  identity(b, "subscription_id", e->token.subscription);
  identity(b, "stream_generation", e->token.stream);
  identity(b, "host_incarnation", s->host);
  count(b, "first_sequence", 1);
  (void)b.key("replay_supported");
  (void)b.boolean(false);
  (void)b.key("effective_min_interval_ms");
  (void)b.uint64(e->interval.count());
  (void)b.key("filter");
  (void)b.begin_object();
  if (request.filter.owner) {
    (void)b.key("owner");
    (void)b.begin_object();
    identity(b, "principal_id", request.filter.owner->principal_id);
    (void)b.end_object();
  } else {
    (void)b.key("executions");
    (void)b.begin_array();
    for (auto ref : request.filter.executions) {
      (void)b.begin_object();
      identity(b, "execution_id", ref.execution_id);
      (void)b.end_object();
    }
    (void)b.end_array();
  }
  (void)b.end_object();
  (void)b.key("topics");
  (void)b.begin_array();
  for (auto t : request.filter.topics)
    (void)b.string(topic(t));
  (void)b.end_array();
  (void)b.key("budgets");
  (void)b.begin_object();
  (void)b.key("pending_messages");
  (void)b.uint64(s->budget.pending_per_subscription);
  (void)b.key("frame_bytes");
  (void)b.uint64(s->budget.frame_bytes);
  (void)b.key("connection_bytes");
  (void)b.uint64(s->budget.queued_bytes);
  (void)b.key("principal_bytes");
  (void)b.uint64(s->session->limits().queued_bytes);
  (void)b.key("host_bytes");
  (void)b.uint64(s->session->limits().queued_bytes);
  (void)b.end_object();
  (void)b.end_object();
  (void)b.end_object();
  auto value = b.freeze();
  if (!value) {
    (void)unsubscribe(e->token);
    return fail<UnsubscribeRequest>();
  }
  auto frame = encode_frame(*value);
  if (!frame) {
    (void)unsubscribe(e->token);
    return fail<UnsubscribeRequest>();
  }
  auto queued = s->transport->queue_ack(*frame);
  if (!queued) {
    (void)unsubscribe(e->token);
    return foundation::make_unexpected(queued.error());
  }
  {
    std::lock_guard lock(s->mutex);
    if (!e->active)
      return fail<UnsubscribeRequest>();
    e->ack = true;
  }
  rollback.committed = true;
  return e->token;
}
Result<bool>
SubscriptionConnection::unsubscribe(const UnsubscribeRequest &token) {
  auto s = state_;
  std::shared_ptr<State::Entry> removed;
  {
    std::lock_guard lock(s->mutex);
    auto it =
        std::find_if(s->entries.begin(), s->entries.end(), [&](const auto &e) {
          return e->token.subscription == token.subscription &&
                 e->token.stream == token.stream;
        });
    if (it == s->entries.end())
      return false;
    removed = *it;
    removed->active = false;
    s->entries.erase(it);
    (void)s->sender->unsubscribe(*removed->watch);
  }
  // 释放监听租约在序列锁之外，允许来源等待其他线程的回调退出。
  removed->lease.reset();
  return true;
}
Result<policy::StartResult> SubscriptionConnection::pump() {
  auto s = state_;
  std::vector<UnsubscribeRequest> retired;
  {
    std::lock_guard lock(s->mutex);
    for (auto &e : s->entries)
      if (!e->active)
        retired.push_back(e->token);
  }
  if (!retired.empty()) {
    close();
    return fail<policy::StartResult>();
  }
  Result<policy::StartResult> result = policy::StartResult::NotStarted;
  bool terminate = false;
  {
    std::lock_guard lock(s->mutex);
    if (s->closed)
      return fail<policy::StartResult>();
    if (s->slow_since &&
        s->clock->now() - *s->slow_since >= s->budget.transport_timeout) {
      terminate = true;
      result = foundation::make_unexpected(error(ProtocolErrc::Closed));
    } else {
      result = s->sender->start_next();
      if (result && *result == policy::StartResult::NotStarted && s->bytes) {
        if (!s->slow_since)
          s->slow_since = s->clock->now();
      } else {
        s->slow_since.reset();
      }
      if (!result) {
        for (auto &e : s->entries)
          e->gap = true;
        const auto code = result.error().code();
        terminate = code == policy::policy_error(policy::PolicyErrc::Expired).code() ||
                    code == policy::policy_error(policy::PolicyErrc::SessionClosed).code() ||
                    code == policy::policy_error(policy::PolicyErrc::StoreClosed).code();
      } else if (*result == policy::StartResult::Unknown) {
        terminate = true;
      }
    }
  }
  if (terminate)
    close();
  return result;
}
void SubscriptionConnection::close() {
  auto s = state_;
  std::vector<std::shared_ptr<State::Entry>> retired;
  {
    std::lock_guard lock(s->mutex);
    if (s->closed)
      return;
    s->closed = true;
    retired.swap(s->entries);
    for (auto &e : retired) {
      e->active = false;
      (void)s->sender->unsubscribe(*e->watch);
    }
  }
  for (auto &e : retired)
    e->lease.reset();
  s->transport->close();
}
std::size_t SubscriptionConnection::queued_bytes() const noexcept {
  return state_->bytes;
}
} // namespace ock::control
