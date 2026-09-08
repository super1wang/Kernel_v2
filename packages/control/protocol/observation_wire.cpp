#include <algorithm>
#include <charconv>
#include <ock/control_protocol/observation_wire.hpp>
namespace ock::control {
namespace {
template <class T> Result<T> invalid() {
  return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
}
bool fields(data::ValueView value,
            std::initializer_list<std::string_view> allowed) {
  if (value.kind() != data::Kind::Object)
    return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    auto key = *value.key_at(i);
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      return false;
  }
  return true;
}
Result<contracts::PrincipalRef> owner(data::ValueView value,
                                      contracts::PrincipalRef caller) {
  if (value.missing() || value.string() == "self") {
    if (caller.principal_id.empty())
      return invalid<contracts::PrincipalRef>();
    return caller;
  }
  if (!fields(value, {"principal_id"}) || value.size() != 1)
    return invalid<contracts::PrincipalRef>();
  auto id = wire_id<contracts::PrincipalId>(value.at("principal_id"));
  if (!id)
    return invalid<contracts::PrincipalRef>();
  return contracts::PrincipalRef{*id};
}
} // namespace
Result<std::uint64_t> wire_count(data::ValueView value) {
  auto text = value.string();
  if (!text || text->empty() || text->size() > 20 ||
      (text->size() > 1 && text->front() == '0'))
    return invalid<std::uint64_t>();
  std::uint64_t result = 0;
  auto parsed =
      std::from_chars(text->data(), text->data() + text->size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size())
    return invalid<std::uint64_t>();
  return result;
}
Result<SubscribeRequest> parse_subscribe(data::ValueView value,
                                         contracts::PrincipalRef caller) {
  if (!fields(value, {"filter", "topics", "min_interval_ms"}))
    return invalid<SubscribeRequest>();
  auto filter = value.at("filter"), topics = value.at("topics");
  if (!fields(filter, {"executions", "owner"}) || filter.size() != 1 ||
      topics.kind() != data::Kind::Array || !topics.size() || topics.size() > 3)
    return invalid<SubscribeRequest>();
  SubscribeRequest result;
  auto executions = filter.at("executions");
  if (!executions.missing()) {
    if (executions.kind() != data::Kind::Array || !executions.size() ||
        executions.size() > 32)
      return invalid<SubscribeRequest>();
    for (std::size_t i = 0; i < executions.size(); ++i) {
      auto ref = executions.at(i);
      if (!fields(ref, {"execution_id"}) || ref.size() != 1)
        return invalid<SubscribeRequest>();
      auto id = wire_id<foundation::TaskId>(ref.at("execution_id"));
      if (!id)
        return invalid<SubscribeRequest>();
      result.filter.executions.push_back({*id});
    }
  } else {
    auto id = owner(filter.at("owner"), caller);
    if (!id)
      return invalid<SubscribeRequest>();
    result.filter.owner = *id;
  }
  for (std::size_t i = 0; i < topics.size(); ++i) {
    auto text = topics.at(i).string();
    if (text == "execution.progress")
      result.filter.topics.push_back(contracts::ObservationTopic::Progress);
    else if (text == "execution.phase")
      result.filter.topics.push_back(contracts::ObservationTopic::Phase);
    else if (text == "execution.fact")
      result.filter.topics.push_back(contracts::ObservationTopic::Fact);
    else
      return invalid<SubscribeRequest>();
  }
  auto interval = value.at("min_interval_ms");
  if (!interval.missing()) {
    auto n = interval.uint64();
    if (!n || *n > INT32_MAX)
      return invalid<SubscribeRequest>();
    result.interval =
        std::chrono::milliseconds((std::max)(*n, std::uint64_t{50}));
  }
  if (!contracts::validate_observation_filter(result.filter, 32))
    return invalid<SubscribeRequest>();
  return result;
}
Result<UnsubscribeRequest> parse_unsubscribe(data::ValueView value) {
  if (!fields(value, {"subscription_id", "stream_generation"}) ||
      value.size() != 2)
    return invalid<UnsubscribeRequest>();
  auto subscription = wire_id<SubscriptionId>(value.at("subscription_id"));
  auto stream = wire_id<StreamGeneration>(value.at("stream_generation"));
  if (!subscription || !stream)
    return invalid<UnsubscribeRequest>();
  return UnsubscribeRequest{*subscription, *stream};
}
Result<WireListRequest> parse_list(data::ValueView value,
                                   contracts::PrincipalRef caller) {
  if (!fields(value, {"owner", "phase_set", "page_size", "cursor"}))
    return invalid<WireListRequest>();
  auto principal = owner(value.at("owner"), caller);
  if (!principal)
    return invalid<WireListRequest>();
  WireListRequest result{
      {*principal, contracts::PhaseSet::Nonterminal, {50, 2000}, {}}, {}};
  auto phase = value.at("phase_set");
  if (!phase.missing()) {
    if (phase.string() == "terminal")
      result.request.phases = contracts::PhaseSet::Terminal;
    else if (phase.string() == "all")
      result.request.phases = contracts::PhaseSet::All;
    else if (phase.string() != "nonterminal")
      return invalid<WireListRequest>();
  }
  auto size = value.at("page_size");
  if (!size.missing()) {
    auto n = size.uint64();
    if (!n || !*n || *n > 200)
      return invalid<WireListRequest>();
    result.request.budget.page_size = static_cast<std::uint32_t>(*n);
  }
  auto cursor = value.at("cursor");
  if (!cursor.missing()) {
    auto text = cursor.string();
    if (!text || text->empty() || text->size() > 2048 ||
        !text->starts_with("v1."))
      return invalid<WireListRequest>();
    auto dot = text->find('.', 3);
    if (dot == text->npos || dot == 3 || text->size() - dot - 1 != 43 ||
        text->find('.', dot + 1) != text->npos)
      return invalid<WireListRequest>();
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    if (text->substr(3, dot - 3).find_first_not_of(alphabet) != text->npos ||
        text->substr(dot + 1).find_first_not_of(alphabet) != text->npos)
      return invalid<WireListRequest>();
    result.cursor = std::string(*text);
  }
  return result;
}
} // namespace ock::control
