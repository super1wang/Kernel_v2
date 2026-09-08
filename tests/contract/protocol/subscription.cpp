#include "tests/contract/authorization/fixtures.hpp"
#include <fstream>
#include <future>
#include <ock/dynamic/binding/schema.hpp>
#include <ock/control/subscription.hpp>
#include <ock/control/subscription_methods.hpp>
using namespace ock;
struct Events final : contracts::ObservationPort {
  std::vector<std::weak_ptr<contracts::ObservationReceiver>> receivers;
  std::shared_ptr<const contracts::ExecutionSummary> summary;
  unsigned leases = 0;
  bool burst = true, fail = false;
  std::function<void()> before_return;
  struct Lease final : contracts::ObservationLease {
    Events *source;
    explicit Lease(Events *s) : source(s) { ++source->leases; }
    ~Lease() { --source->leases; }
  };
  Result<std::shared_ptr<const contracts::ExecutionSummary>>
  get_summary(const contracts::CallerView &, contracts::ExecutionRef) override {
    return summary;
  }
  Result<contracts::ListPage>
  list_summaries(const contracts::CallerView &,
                 const contracts::ListRequest &) override {
    return foundation::make_unexpected(
        contracts::error(contracts::ContractsErrc::Rejected));
  }
  Result<std::unique_ptr<contracts::ObservationLease>> observe_changes(
      const contracts::CallerView &, const contracts::ObservationFilter &,
      std::shared_ptr<contracts::ObservationReceiver> receiver) override {
    if (fail)
      return foundation::make_unexpected(
          contracts::error(contracts::ContractsErrc::Rejected));
    receivers.push_back(receiver);
    if (burst)
      receiver->changed(
          {summary, contracts::ObservationTopic::Progress, false});
    if (before_return)
      before_return();
    return std::unique_ptr<contracts::ObservationLease>(
        std::make_unique<Lease>(this));
  }
  void emit(contracts::ObservationTopic topic) {
    for (auto &receiver : receivers)
      if (auto r = receiver.lock())
        r->changed({summary, topic, false});
  }
};
struct Transport final : control::ObservationTransport {
  struct Reservation final : runtime::policy::TransmissionReservation {
    std::size_t n;
    explicit Reservation(std::size_t size) : n(size) {}
    std::size_t capacity() const noexcept override { return n; }
  };
  std::vector<std::vector<std::byte>> frames;
  bool fail_ack = false, slow = false, closed = false;
  void close() noexcept override { closed = true; }
  Result<void> queue_ack(std::span<const std::byte> bytes) override {
    if (fail_ack)
      return foundation::make_unexpected(
          control::error(control::ProtocolErrc::BudgetExceeded));
    frames.emplace_back(bytes.begin(), bytes.end());
    return {};
  }
  Result<std::unique_ptr<runtime::policy::TransmissionReservation>>
  reserve(std::size_t n) override {
    return std::unique_ptr<runtime::policy::TransmissionReservation>(
        std::make_unique<Reservation>(n));
  }
  runtime::policy::StartResult
  start_now(const runtime::policy::PreparedTransmission &frame,
            runtime::policy::TransmissionReservation &) noexcept override {
    if (slow)
      return runtime::policy::StartResult::NotStarted;
    try {
      frames.emplace_back(frame.bytes().begin(), frame.bytes().end());
      return runtime::policy::StartResult::Started;
    } catch (...) {
      return runtime::policy::StartResult::Unknown;
    }
  }
};
int main(int argc, char **argv) try {
  CHECK(argc == 3);
  auto read = [](const char *path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
  };
  binding::SchemaResources schemas{
      {"urn:ock:rpc:notifications:1", read(argv[1])},
      {"urn:ock:rpc:common:1", read(argv[2])}};
  auto ack_schema = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:notifications:1#/$defs/subscribed"})",
      schemas);
  CHECK(ack_schema);
  auto event_schema = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:notifications:1#/$defs/event"})",
      schemas);
  CHECK(event_schema);
  policy_test::PolicyBudget policy_limits;
  policy_limits.watches_per_principal = 32;
  policy_test::Env env(policy_limits);
  auto events = std::make_shared<Events>();
  events->summary = env.source->rows[0].second.summary;
  auto transport = std::make_shared<Transport>();
  auto connection = control::SubscriptionConnection::create(
      env.session, env.caller, events, transport, env.clock,
      env.source->source_id.host);
  CHECK(connection);
  control::SubscribeRequest request{{{{id<foundation::TaskId>()}},
                                     {},
                                     {contracts::ObservationTopic::Progress,
                                      contracts::ObservationTopic::Phase}},
                                    std::chrono::milliseconds(100)};
  auto subscribed = (*connection)->subscribe("1", request);
  CHECK(subscribed && events->leases == 1 && transport->frames.size() == 1);
  control::FrameDecoder decoder;
  auto ack = decoder.consume(transport->frames[0]);
  CHECK(ack && ack->message &&
        ack->message->view().at("result").at("first_sequence").string() == "1");
  CHECK(ack_schema->validate(ack->message->view()));
  CHECK((*connection)->pump() == runtime::policy::StartResult::Started);
  CHECK(transport->frames.size() == 2);
  auto first = decoder.consume(transport->frames[1]);
  CHECK(first &&
        first->message->view().at("params").at("sequence").string() == "1");
  CHECK(event_schema->validate(first->message->view()));
  events->emit(contracts::ObservationTopic::Progress);
  env.clock->elapsed = 100;
  events->emit(contracts::ObservationTopic::Phase);
  CHECK((*connection)->pump() == runtime::policy::StartResult::Started);
  auto next = decoder.consume(transport->frames.back());
  CHECK(next &&
        next->message->view().at("params").at("sequence").string() == "3" &&
        next->message->view().at("params").at("gap").boolean() == true);
  CHECK(event_schema->validate(next->message->view()));
  events->emit(contracts::ObservationTopic::Phase);
  auto policy = policy_test::configuration().principals[0];
  policy.rules.clear();
  CHECK(env.assembly.administration->replace_principal_policy(policy));
  auto count = transport->frames.size();
  CHECK(!(*connection)->pump());
  CHECK(transport->frames.size() == count);
  CHECK((*connection)->unsubscribe(*subscribed) == true);
  CHECK((*connection)->unsubscribe(*subscribed) == false);
  CHECK(events->leases == 0 && (*connection)->queued_bytes() == 0);
  CHECK(env.assembly.administration->replace_principal_policy(
      policy_test::configuration().principals[0]));
  transport->fail_ack = true;
  CHECK(!(*connection)->subscribe("2", request));
  CHECK(events->leases == 0 && (*connection)->queued_bytes() == 0);
  transport->fail_ack = false;
  events->fail = true;
  CHECK(!(*connection)->subscribe("source-failure", request));
  CHECK(events->leases == 0 && (*connection)->queued_bytes() == 0);
  events->fail = false;
  for (int n = 0; n < 8; ++n)
    CHECK((*connection)->subscribe(std::to_string(n + 3), request));
  CHECK(!(*connection)->subscribe("11", request));
  (*connection)->close();
  CHECK(events->leases == 0 && (*connection)->queued_bytes() == 0);
  CHECK(transport->closed);
  auto limited_transport = std::make_shared<Transport>();
  control::SubscriptionBudget limited;
  limited.sequence_limit = 1;
  auto limited_connection = control::SubscriptionConnection::create(
      env.session, env.caller, events, limited_transport, env.clock,
      env.source->source_id.host, limited);
  CHECK(limited_connection);
  CHECK((*limited_connection)->subscribe("sequence-limit", request));
  CHECK((*limited_connection)->pump() == runtime::policy::StartResult::Started);
  events->emit(contracts::ObservationTopic::Phase);
  CHECK(!(*limited_connection)->pump());
  CHECK(limited_transport->closed && events->leases == 0 &&
        (*limited_connection)->queued_bytes() == 0);
  auto fact_transport = std::make_shared<Transport>();
  auto fact_connection = control::SubscriptionConnection::create(
      env.session, env.caller, events, fact_transport, env.clock,
      env.source->source_id.host);
  CHECK(fact_connection);
  auto fact_request = request;
  fact_request.filter.topics = {contracts::ObservationTopic::Fact};
  CHECK((*fact_connection)->subscribe("facts", fact_request));
  auto input = events->summary->value();
  auto business = id<contracts::EffectId>(42);
  input.facts = {contracts::summarize_fact(contracts::EffectFact{
      id<contracts::FactId>(7), business, contracts::Application::Applied})};
  auto bad_input = input;
  bad_input.facts[0].reference = id<contracts::CommitId>(42);
  CHECK(!contracts::ExecutionSummary::create(bad_input));
  auto fact_summary = contracts::ExecutionSummary::create(input);
  CHECK(fact_summary);
  events->summary = *fact_summary;
  env.source->rows[0].second.summary = *fact_summary;
  events->emit(contracts::ObservationTopic::Fact);
  CHECK((*fact_connection)->pump() == runtime::policy::StartResult::Started);
  auto fact_frame = decoder.consume(fact_transport->frames.back());
  CHECK(fact_frame && event_schema->validate(fact_frame->message->view()));
  auto fact_data = fact_frame->message->view().at("params").at("data");
  CHECK(fact_data.at("kind").string() == "EffectResolved");
  CHECK(fact_data.at("reference").string() == control::wire_text(business));
  CHECK(fact_data.at("published").boolean() == false);
  input.facts = {contracts::summarize_fact(contracts::PublishedFact{
      id<contracts::FactId>(8), id<contracts::CommitId>(43), 1})};
  fact_summary = contracts::ExecutionSummary::create(input);
  CHECK(fact_summary);
  events->summary = *fact_summary;
  env.source->rows[0].second.summary = *fact_summary;
  events->emit(contracts::ObservationTopic::Fact);
  CHECK((*fact_connection)->pump() == runtime::policy::StartResult::Started);
  fact_frame = decoder.consume(fact_transport->frames.back());
  CHECK(fact_frame && event_schema->validate(fact_frame->message->view()));
  fact_data = fact_frame->message->view().at("params").at("data");
  CHECK(fact_data.at("kind").string() == "StateCommitted");
  CHECK(fact_data.at("published").boolean() == true);
  // 缺失业务引用不能退回 FactId；下一可编码提示报告丢失。
  input.facts = {{id<contracts::FactId>(9), contracts::FactKind::Effect,
                  contracts::Application::Applied}};
  fact_summary = contracts::ExecutionSummary::create(input);
  CHECK(fact_summary);
  events->summary = *fact_summary;
  env.source->rows[0].second.summary = *fact_summary;
  const auto sent = fact_transport->frames.size();
  events->emit(contracts::ObservationTopic::Fact);
  CHECK((*fact_connection)->pump() == runtime::policy::StartResult::NotStarted);
  CHECK(fact_transport->frames.size() == sent);
  input.facts = {contracts::summarize_fact(contracts::LifecycleFact{
      id<contracts::FactId>(10), id<contracts::TransitionId>(44),
      name("before"), name("after"), 1})};
  fact_summary = contracts::ExecutionSummary::create(input);
  CHECK(fact_summary);
  events->summary = *fact_summary;
  env.source->rows[0].second.summary = *fact_summary;
  events->emit(contracts::ObservationTopic::Fact);
  CHECK((*fact_connection)->pump() == runtime::policy::StartResult::Started);
  fact_frame = decoder.consume(fact_transport->frames.back());
  CHECK(fact_frame && event_schema->validate(fact_frame->message->view()));
  CHECK(fact_frame->message->view().at("params").at("gap").boolean() == true);
  CHECK(fact_frame->message->view().at("params").at("data").at("kind").string() == "LifecycleResolved");
  (*fact_connection)->close();
  CHECK(events->leases == 0);
  auto slow_transport = std::make_shared<Transport>();
  slow_transport->slow = true;
  control::SubscriptionBudget slow_budget;
  slow_budget.transport_timeout = std::chrono::milliseconds(100);
  auto slow_connection = control::SubscriptionConnection::create(
      env.session, env.caller, events, slow_transport, env.clock,
      env.source->source_id.host, slow_budget);
  CHECK(slow_connection);
  CHECK((*slow_connection)->subscribe("slow", request));
  CHECK((*slow_connection)->pump() == runtime::policy::StartResult::NotStarted);
  env.clock->elapsed += 100;
  CHECK(!(*slow_connection)->pump());
  CHECK(slow_transport->closed && events->leases == 0 &&
        (*slow_connection)->queued_bytes() == 0);
  auto routed_transport = std::make_shared<Transport>();
  auto routed_connection = control::SubscriptionConnection::create(
      env.session, env.caller, events, routed_transport, env.clock,
      env.source->source_id.host);
  CHECK(routed_connection);
  std::shared_ptr<control::SubscriptionConnection> routed(std::move(*routed_connection));
  auto methods = control::subscription_methods(routed, env.caller, schemas);
  CHECK(methods);
  control::Hello hello{"test.app", control::wire_text(env.source->source_id.host),
                       control::wire_text(env.source->source_id.host)};
  hello.observation_backend = "mock";
  auto router = control::Router::create(hello, env.caller, std::move(*methods));
  CHECK(router);
  auto send = [&](std::string_view text) {
    auto payload = data::Payload::parse(text);
    CHECK(payload);
    return router->dispatch(payload->view());
  };
  CHECK(send(R"({"jsonrpc":"2.0","id":"hello","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  auto routed_ack = send(R"({"jsonrpc":"2.0","id":"subscribe","method":"notifications.subscribe","params":{"filter":{"owner":"self"},"topics":["execution.progress"]}})");
  CHECK(routed_ack && routed_ack->queued && routed_ack->json.empty());
  CHECK(routed_transport->frames.size() == 1 && events->leases == 1);
  CHECK(routed->pump() == runtime::policy::StartResult::Started);
  CHECK(routed_transport->frames.size() == 2);
  auto routed_payload = decoder.consume(routed_transport->frames.front());
  CHECK(routed_payload && ack_schema->validate(routed_payload->message->view()));
  auto token = control::parse_unsubscribe(routed_payload->message->view().at("result"));
  // 退订参数是专门 DTO，不能把包含其他字段的 ack result 原样当参数。
  CHECK(!token);
  auto ack_result = routed_payload->message->view().at("result");
  std::string remove = "{\"jsonrpc\":\"2.0\",\"id\":\"remove\",\"method\":\"notifications.unsubscribe\",\"params\":{\"subscription_id\":\"" +
      std::string(*ack_result.at("subscription_id").string()) + "\",\"stream_generation\":\"" +
      std::string(*ack_result.at("stream_generation").string()) + "\"}}";
  auto removed = send(remove);
  CHECK(removed && !removed->queued);
  auto removed_payload = data::Payload::parse(removed->json);
  CHECK(removed_payload && removed_payload->view().at("result").at("removed").boolean() == true);
  CHECK(events->leases == 0);
  CHECK(send(R"({"jsonrpc":"2.0","id":"again","method":"notifications.subscribe","params":{"filter":{"owner":"self"},"topics":["execution.progress"]}})"));
  auto wrong_direction = send(R"({"jsonrpc":"2.0","method":"notifications.event","params":{}})");
  CHECK(wrong_direction && wrong_direction->close);
  CHECK(routed_transport->closed && events->leases == 0 && routed->queued_bytes() == 0);
  auto racing_transport = std::make_shared<Transport>();
  auto racing = control::SubscriptionConnection::create(
      env.session, env.caller, events, racing_transport, env.clock,
      env.source->source_id.host);
  CHECK(racing);
  std::promise<void> entered, resume;
  auto resume_signal = resume.get_future();
  events->before_return = [&] { entered.set_value(); resume_signal.wait(); };
  auto registration = std::async(std::launch::async, [&] {
    return (*racing)->subscribe("closing-registration", request);
  });
  entered.get_future().wait();
  (*racing)->close();
  resume.set_value();
  CHECK(!registration.get());
  CHECK(events->leases == 0 && racing_transport->frames.empty() &&
        (*racing)->queued_bytes() == 0);
  events->before_return = {};
  std::cout << "Subscription ack ordering, loss sequence, revoke and cleanup "
               "passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
