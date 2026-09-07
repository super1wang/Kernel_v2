#pragma once
#include "fixtures.hpp"

namespace policy_test::observation_cases {
inline std::unique_ptr<SendCoordinator> sender(
    const std::shared_ptr<SessionAuthority>& session, const std::shared_ptr<Sink>& sink) {
  auto made = SendCoordinator::create(session, sink, std::make_shared<Encoder>());
  CHECK(made); return std::move(*made);
}

inline void noninvoke_permissions() {
  Env e;
  auto query = [&] { return e.session->observations()->get(
      *e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary); };
  CHECK(query());
  auto cfg = configuration();
  auto use = *std::find_if(cfg.uses.begin(), cfg.uses.end(), [](const auto& u) {
    return u.use == AccessUse::GetSummary;
  });
  use.required_permissions = {name("observe")};
  CHECK(e.assembly.administration->replace_use_policy(use));
  CHECK(!query()); // 非Invoke取UsePolicy真实权限，不能沿用operation的allow。
  auto broaden = [](auto& rs) {
    for (auto& r : rs) if (r.use == AccessUse::GetSummary)
      r.permissions.push_back(name("observe"));
  };
  broaden(cfg.principals[0].rules);
  CHECK(e.assembly.administration->replace_principal_policy(cfg.principals[0]));
  CHECK(!query());
  broaden(cfg.operations[0].module_rules);
  CHECK(e.assembly.administration->replace_operation_policy(cfg.operations[0]));
  CHECK(!query());
  broaden(cfg.targets[0].rules);
  CHECK(e.assembly.administration->replace_target_policy(cfg.targets[0]));
  CHECK(!query());
  broaden(use.module_rules);
  CHECK(e.assembly.administration->replace_use_policy(use));
  CHECK(!query()); // 原会话委托/认证ceiling不能因管理侧扩权而自动扩张。
  auto broad = rules(); broaden(broad);
  auto open_and_query = [&](const std::vector<ScopeRule>& scope) {
    auto session = e.assembly.store->open({{std::byte{7}}},
        {scope, e.auth->identity.deadline, false});
    CHECK(session);
    auto caller = (*session)->verify({principal(), {}, {}}); CHECK(caller);
    return (*session)->observations()->get(**caller, {id<foundation::TaskId>()},
                                           AccessUse::GetSummary);
  };
  CHECK(!open_and_query(broad)); // 即使新委托有权限，旧认证ceiling仍不授予。
  e.auth->identity.ceiling.rules = broad; // 只影响后续可信认证，不改旧会话。
  CHECK(!open_and_query(rules()));
  CHECK(open_and_query(broad));
  CHECK(!query());
}

inline void page_binding_dimensions() {
  Env e;
  ListRequest query{principal(), PhaseSet::All, {1, 1}, {}};
  auto page = e.session->observations()->list(*e.caller, query, {});
  CHECK(page && page->continuation && page->page.items.size() == 1);
  auto next = e.session->observations()->list(*e.caller, query, page->continuation);
  CHECK(next && next->page.items.size() == 1);
  CHECK(next->page.items[0].listing_ordinal < page->page.items[0].listing_ordinal);
  auto changed = query; changed.owner = principal(2);
  CHECK(!e.session->observations()->list(*e.caller, changed, page->continuation));
  changed = query; changed.phases = PhaseSet::Nonterminal;
  CHECK(!e.session->observations()->list(*e.caller, changed, page->continuation));
  CHECK(e.session->observations()->list(*e.caller, changed, {}));
  auto narrower = rules();
  std::erase_if(narrower, [](const auto& r) { return r.operation == operation(2); });
  CHECK(e.session->restrict_delegation({narrower, e.auth->identity.deadline, false}));
  auto fresh = e.session->verify({principal(), {}, {}}); CHECK(fresh);
  CHECK(!e.session->observations()->list(**fresh, query, page->continuation));
  auto fresh_page = e.session->observations()->list(**fresh, query, {});
  CHECK(fresh_page && fresh_page->continuation);
  CHECK(e.session->observations()->list(**fresh, query, fresh_page->continuation));
}

inline void source_store_lifetime() {
  Env e;
  ListRequest query{principal(), PhaseSet::All, {1, 1}, {}};
  ObservationFilter filter{{{id<foundation::TaskId>()}}, {}, {ObservationTopic::Progress}};
  auto get = e.session->observations()->get(*e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
  auto list = e.session->observations()->list(*e.caller, query, {});
  auto watch = e.session->observations()->subscribe(*e.caller, filter);
  CHECK(get && list && list->continuation && watch);
  auto sink = std::make_shared<Sink>(); auto send = sender(e.session, sink);
  CHECK(send->enqueue_response(get->response)); CHECK(send->enqueue_response(list->response));
  CHECK(send->enqueue(**watch, {e.source->rows[0].second.summary, ObservationTopic::Progress, false}));
  CHECK(sink->pending == 6 && sink->size == 0);
  CHECK(e.assembly.administration->close_store());
  CHECK(sink->pending == 0 && sink->size == 0 && sink->starts == 0);
  // 合法寿命顺序：先关闭旧Store，再更换source host及相应summary，最后新建Store。
  e.source->source_id.host = id<HostIncarnation>(2);
  for (auto& row : e.source->rows) {
    auto value = row.second.summary->value(); value.host = e.source->source_id.host;
    auto made = ExecutionSummary::create(value); CHECK(made); row.second.summary = *made;
  }
  auto assembly = PolicyStore::create({}, configuration(), e.auth, e.clock, e.digest, e.source);
  CHECK(assembly);
  auto session = assembly->store->open({{std::byte{7}}}, {rules(), e.auth->identity.deadline, false});
  CHECK(session);
  auto caller = (*session)->verify({principal(), {}, {}}); CHECK(caller);
  auto fresh_sink = std::make_shared<Sink>(); auto fresh_send = sender(*session, fresh_sink);
  CHECK(!fresh_send->enqueue_response(get->response));
  CHECK(!fresh_send->enqueue_response(list->response));
  CHECK(!fresh_send->enqueue(**watch, {e.source->rows[0].second.summary, ObservationTopic::Progress, false}));
  CHECK(!(*session)->observations()->get(*e.caller, {id<foundation::TaskId>()}, AccessUse::GetSummary));
  CHECK(!(*session)->observations()->list(**caller, query, list->continuation));
  CHECK(!send->start_next());
  CHECK(!send->enqueue_response(get->response)); CHECK(!send->enqueue_response(list->response));
  CHECK(!send->enqueue(**watch, {e.source->rows[0].second.summary, ObservationTopic::Progress, false}));
  CHECK(sink->size == 0 && sink->starts == 0 && fresh_sink->size == 0);
  auto new_get = (*session)->observations()->get(**caller, {id<foundation::TaskId>()}, AccessUse::GetSummary);
  auto new_list = (*session)->observations()->list(**caller, query, {});
  auto new_watch = (*session)->observations()->subscribe(**caller, filter);
  CHECK(new_get && new_list && new_watch);
  CHECK(new_get->summary->value().host == e.source->source_id.host);
  CHECK(fresh_send->enqueue_response(new_get->response));
  CHECK(fresh_send->enqueue_response(new_list->response));
  CHECK(fresh_send->enqueue(**new_watch, {e.source->rows[0].second.summary, ObservationTopic::Progress, false}));
  for (unsigned i = 0; i != 3; ++i) CHECK(fresh_send->start_next() == StartResult::Started);
  CHECK(fresh_sink->size == 6 && fresh_sink->starts == 3 && fresh_sink->pending == 0);
  for (unsigned i = 0; i != 3; ++i) CHECK(fresh_sink->data[2*i] == std::byte{0x51});
  CHECK(fresh_sink->data[1] == std::byte(static_cast<unsigned>(ProjectionKind::Summary)));
  CHECK(fresh_sink->data[3] == std::byte(static_cast<unsigned>(ProjectionKind::Page)));
  CHECK(fresh_sink->data[5] == std::byte(static_cast<unsigned>(ProjectionKind::Hint)));
}
} // namespace policy_test::observation_cases
