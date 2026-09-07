#pragma once
#include "fixtures.hpp"
#include <semaphore>

namespace policy_test::send_cases {
inline ObservationFilter filter() {
  return {{{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
}
inline std::shared_ptr<WatchAuthorization> watch(Env& env) {
  auto result = env.session->observations()->subscribe(*env.caller, filter());
  CHECK(result); return *result;
}
inline ChangeHint hint(Env& env) {
  return {env.source->rows[0].second.summary, ObservationTopic::Progress, false};
}
inline std::unique_ptr<SendCoordinator> sender(const std::shared_ptr<SessionAuthority>& session,
                                               const std::shared_ptr<Sink>& sink) {
  auto result = SendCoordinator::create(session, sink, std::make_shared<Encoder>());
  CHECK(result); return std::move(*result);
}
inline std::shared_ptr<const ResponseAuthorization> response(Env& env) {
  auto result = env.session->observations()->get(*env.caller,
      {id<foundation::TaskId>()}, AccessUse::GetSummary);
  CHECK(result); return result->response;
}
inline void received(const Sink& sink, ProjectionKind kind) {
  CHECK(sink.size == 2 && sink.starts == 1);
  CHECK(sink.data[0] == std::byte{0x51});
  CHECK(sink.data[1] == std::byte(static_cast<unsigned>(kind)));
}

inline void transmission_start_arbitration() {
  // encode 返回的原容器在 reserve 回调期间仍存活；改变其元素不能改变私有发送帧。
  struct AliasingEncoder final : ProjectionEncoderPort {
    std::byte* returned_element = nullptr;
    Result<std::vector<std::byte>> encode(const ProjectionSnapshot& p, std::size_t cap) override {
      CHECK(cap >= 2);
      std::vector<std::byte> bytes{std::byte{0x51}, std::byte(static_cast<unsigned>(p.kind()))};
      returned_element = bytes.data();
      return std::move(bytes);
    }
  };
  for (bool use_watch : {false, true}) {
    Env env;
    auto encoder = std::make_shared<AliasingEncoder>();
    auto sink = std::make_shared<Sink>();
    auto created = SendCoordinator::create(env.session, sink, encoder);
    CHECK(created);
    auto send = std::move(*created);
    bool changed = false;
    sink->before_reserve_return = [&] {
      CHECK(encoder->returned_element != nullptr);
      *encoder->returned_element = std::byte{0x7e};
      changed = true;
    };
    if (use_watch) {
      auto active = watch(env);
      CHECK(send->enqueue(*active, hint(env)));
      CHECK(changed && sink->size == 0);
      CHECK(send->start_next() == StartResult::Started);
      received(*sink, ProjectionKind::Hint);
    } else {
      CHECK(send->enqueue_response(response(env)));
      CHECK(changed && sink->size == 0);
      CHECK(send->start_next() == StartResult::Started);
      received(*sink, ProjectionKind::Summary);
    }
    // enqueue 返回后不再访问原元素地址。
    encoder->returned_element = nullptr;
  }
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    auto material = response(env);
    bool reserved_without_send = false;
    sink->before_reserve_return = [&] { reserved_without_send = sink->size == 0 && sink->starts == 0; };
    CHECK(send->enqueue_response(material));
    CHECK(reserved_without_send && sink->size == 0);
    std::binary_semaphore revoked(0);
    bool revoke_ok = false, rejected = false;
    std::thread revoker([&] {
      revoke_ok = bool(env.assembly.administration->replace_principal_policy({principal(), {}}));
      revoked.release();
    });
    std::thread transport([&] { revoked.acquire(); rejected = !send->start_next(); });
    revoker.join(); transport.join();
    CHECK(revoke_ok && rejected && sink->size == 0 && sink->starts == 0);
  }
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    CHECK(send->enqueue_response(response(env)));
    std::binary_semaphore sent(0);
    bool started = false, revoke_ok = false;
    std::thread transport([&] { started = send->start_next() == StartResult::Started; sent.release(); });
    std::thread revoker([&] {
      sent.acquire();
      revoke_ok = bool(env.assembly.administration->replace_principal_policy({principal(), {}}));
    });
    transport.join(); revoker.join();
    CHECK(started && revoke_ok); received(*sink, ProjectionKind::Summary);
    CHECK(send->start_next() == StartResult::NotStarted);
    received(*sink, ProjectionKind::Summary); // 迟到撤权不撤回已写字节，不开始第二帧。
  }
}

inline void failed_start_not_started() {
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    auto first = response(env); auto second = response(env);
    sink->size = sink->data.size() - 2;
    CHECK(send->enqueue_response(first));
    CHECK(!send->enqueue_response(second)); // 剩余容量已被第一张未start票据预留。
    CHECK(send->start_next() == StartResult::Started);
    CHECK(sink->size == sink->data.size() && sink->starts == 1);
    CHECK(sink->data[sink->data.size() - 2] == std::byte{0x51});
    sink->size = 0; // 验证消费者显式排空接收介质后，失败前未排入的response可重试。
    CHECK(send->enqueue_response(second));
    CHECK(send->start_next() == StartResult::Started);
    CHECK(sink->size == 2 && sink->starts == 2);
  }
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    auto material = response(env);
    sink->size = sink->data.size(); // 实际剩余容量不足，reserve不授予票据。
    CHECK(!send->enqueue_response(material));
    CHECK(sink->starts == 0);
    sink->size = 0;
    CHECK(send->enqueue_response(material));
    sink->result = StartResult::NotStarted;
    CHECK(send->start_next() == StartResult::NotStarted);
    CHECK(sink->size == 0 && sink->starts == 1);
    sink->result = StartResult::Started;
    CHECK(send->start_next() == StartResult::Started);
    CHECK(sink->size == 2 && sink->starts == 2);
    CHECK(sink->data[0] == std::byte{0x51});
    CHECK(!send->enqueue_response(material));
  }
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    CHECK(send->enqueue_response(response(env)));
    sink->result = StartResult::NotStarted;
    CHECK(send->start_next() == StartResult::NotStarted);
    CHECK(env.assembly.administration->replace_principal_policy({principal(), {}}));
    sink->result = StartResult::Started;
    CHECK(!send->start_next());
    CHECK(sink->size == 0 && sink->starts == 1);
  }
}

inline void unknown_start_no_retry() {
  PolicyBudget budget; budget.queued_frames = 2;
  Env env(budget);
  auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
  auto first = response(env); auto second = response(env);
  CHECK(send->enqueue_response(first)); CHECK(send->enqueue_response(second));
  sink->result = StartResult::Unknown;
  CHECK(send->start_next() == StartResult::Unknown);
  received(*sink, ProjectionKind::Summary); // 此失约模拟已有字节，但上层不能证明完整起点事实。
  CHECK(sink->pending == 0); // Unknown必须清理同会话其余Queued并归还未使用预留。
  CHECK(!env.caller->view().revalidate());
  sink->result = StartResult::Started;
  CHECK(!send->start_next());
  CHECK(!send->enqueue_response(first));
  CHECK(!send->enqueue_response(second));
  received(*sink, ProjectionKind::Summary);
  CHECK(env.source->rows[0].second.summary->value().phase == ExecutionPhase::Running);
  auto second_session = env.assembly.store->open({{std::byte{7}}}, {rules(), env.auth->identity.deadline, false});
  CHECK(second_session);
  auto caller = (*second_session)->verify({principal(), {}, {}}); CHECK(caller);
  auto sink_b = std::make_shared<Sink>(); auto send_b = sender(*second_session, sink_b);
  for (unsigned i = 0; i != 2; ++i) {
    auto material = (*second_session)->observations()->get(**caller,
        {id<foundation::TaskId>()}, AccessUse::GetSummary);
    CHECK(material && send_b->enqueue_response(material->response));
  }
  CHECK(send_b->start_next() == StartResult::Started);
  CHECK(send_b->start_next() == StartResult::Started);
  CHECK(sink_b->size == 4 && sink_b->starts == 2 && sink_b->pending == 0);
}

inline void unsubscribe_inflight() {
  {
    PolicyBudget budget; budget.queued_frames = 1;
    Env env(budget);
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    auto first = watch(env); CHECK(send->enqueue(*first, hint(env)));
    auto removed = send->unsubscribe(*first); CHECK(removed && *removed);
    CHECK(!send->enqueue(*first, hint(env)));
    auto repeated = send->unsubscribe(*first); CHECK(repeated && !*repeated);
    CHECK(sink->size == 0);
    auto second = watch(env);
    CHECK(send->enqueue(*second, hint(env))); // 退订已删除旧Queued，无需先pump旧帧。
    CHECK(send->start_next() == StartResult::Started);
    received(*sink, ProjectionKind::Hint);
  }
  {
    Env env;
    auto sink = std::make_shared<Sink>(); auto send = sender(env.session, sink);
    auto active = watch(env);
    CHECK(send->enqueue(*active, hint(env)));
    CHECK(send->start_next() == StartResult::Started);
    auto removed = send->unsubscribe(*active); CHECK(removed && *removed);
    CHECK(!send->enqueue(*active, hint(env)));
    received(*sink, ProjectionKind::Hint);
  }
}

inline void subscription_connection_cleanup() {
  PolicyBudget budget; budget.queued_frames = 1;
  Env env(budget);
  auto second = env.assembly.store->open({{std::byte{7}}}, {rules(), env.auth->identity.deadline, false});
  CHECK(second);
  auto caller_b = (*second)->verify({principal(), {}, {}}); CHECK(caller_b);
  auto watch_a = watch(env);
  auto watch_b = (*second)->observations()->subscribe(**caller_b, filter()); CHECK(watch_b);
  auto sink_a = std::make_shared<Sink>(); auto sink_b = std::make_shared<Sink>();
  auto send_a = sender(env.session, sink_a); auto send_b = sender(*second, sink_b);
  auto foreign = send_b->unsubscribe(*watch_a); CHECK(foreign && !*foreign);
  CHECK(!send_b->enqueue(*watch_a, hint(env)));
  CHECK(send_a->enqueue(*watch_a, hint(env)));
  CHECK(env.session->close());
  CHECK(!send_a->enqueue(*watch_a, hint(env)));
  CHECK((*caller_b)->view().revalidate());
  CHECK(send_b->enqueue(**watch_b, hint(env))); // A清理立即返还Queued预算，B不依赖A再pump。
  CHECK(send_b->start_next() == StartResult::Started);
  received(*sink_b, ProjectionKind::Hint);
  CHECK(sink_a->size == 0 && sink_a->starts == 0);
  CHECK(env.source->rows[0].second.summary->value().phase == ExecutionPhase::Running);
}
} // namespace policy_test::send_cases
