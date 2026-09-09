#include "tests/contract/authorization/fixtures.hpp"
#include <fstream>
#include <ock/control/list_method.hpp>
using namespace ock;
struct Clock final : control::CursorClock {
  std::uint64_t elapsed = 0;
  std::uint64_t utc_seconds() const noexcept override { return 1000 + elapsed; }
  std::uint64_t monotonic_seconds() const noexcept override { return elapsed; }
};
struct Transport final : control::ObservationTransport {
  struct Reservation final : runtime::policy::TransmissionReservation {
    std::size_t n;
    explicit Reservation(std::size_t size) : n(size) {}
    std::size_t capacity() const noexcept override { return n; }
  };
  std::vector<std::vector<std::byte>> frames;
  std::function<void()> on_reserve;
  Result<void> queue_ack(std::span<const std::byte>) override {
    throw std::logic_error("unexpected ack");
  }
  void close() noexcept override {}
  Result<std::unique_ptr<runtime::policy::TransmissionReservation>>
  reserve(std::size_t n) override {
    if (on_reserve) { auto callback=std::move(on_reserve); callback(); }
    return std::unique_ptr<runtime::policy::TransmissionReservation>(
        std::make_unique<Reservation>(n));
  }
  runtime::policy::StartResult
  start_now(const runtime::policy::PreparedTransmission &frame,
            runtime::policy::TransmissionReservation &) noexcept override {
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
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
  };
  binding::SchemaResources resources{
      {"urn:ock:rpc:execution-list:1", read(argv[1])},
      {"urn:ock:rpc:common:1", read(argv[2])}};
  auto schema = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:execution-list:1#/$defs/response"})",
      resources);
  CHECK(schema);
  auto canonical_parameters = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:execution-list:1#/$defs/request/properties/params"})", resources);
  CHECK(canonical_parameters);
  auto sample_params = data::Payload::parse(R"({"owner":"self","phase_set":"terminal","page_size":200})");
  CHECK(sample_params && canonical_parameters->validate(sample_params->view()));
  policy_test::Env env;
  env.source->retention_scope = name("managed_active_and_retained_terminal");
  auto clock = std::make_shared<Clock>();
  auto codec = control::CursorCodec::create(
      control::wire_text(env.source->source_id.host), clock);
  CHECK(codec);
  control::CursorContext context;
  context.host = control::wire_text(env.source->source_id.host);
  auto transport = std::make_shared<Transport>();
  auto method = control::ListMethod::create(env.session, env.caller, transport,
                                            *codec, context);
  CHECK(method);
  auto send = [&](std::string_view text) {
    auto p = data::Payload::parse(text);
    CHECK(p);
    return (*method)->dispatch(*env.caller, "list", p->view());
  };
  auto first = send(R"({"page_size":1})");
  CHECK(first && !*first && transport->frames.empty());
  CHECK((*method)->pump() == runtime::policy::StartResult::Started);
  control::FrameDecoder decoder;
  auto frame = decoder.consume(transport->frames.back());
  CHECK(frame && frame->message && schema->validate(frame->message->view()));
  auto result = frame->message->view().at("result");
  CHECK(result.at("items").size() == 1);
  std::string token(*result.at("next_cursor").string());
  auto request = [&](std::string_view cursor,
                     std::string_view phase = "nonterminal") {
    return "{\"page_size\":1,\"phase_set\":\"" + std::string(phase) +
           "\",\"cursor\":\"" + std::string(cursor) + "\"}";
  };
  const auto scans = env.source->scans;
  auto tampered = token;
  tampered[8] = tampered[8] == 'A' ? 'B' : 'A';
  CHECK(!send(request(tampered)));
  CHECK(!send(request(token, "all")));
  CHECK(env.source->scans == scans);
  env.auth->identity.principal = policy_test::principal(2);
  auto other_session = env.assembly.store->open(
      {{std::byte{7}}},
      {policy_test::rules(), env.auth->identity.deadline, false});
  CHECK(other_session);
  auto other_caller =
      (*other_session)->verify({policy_test::principal(2), {}, {}});
  CHECK(other_caller);
  auto other_method = control::ListMethod::create(*other_session, *other_caller,
                                                  std::make_shared<Transport>(),
                                                  *codec, context);
  CHECK(other_method);
  auto other_input = data::Payload::parse(request(token));
  CHECK(other_input);
  CHECK(!(*other_method)
             ->dispatch(**other_caller, "cross-caller", other_input->view()));
  CHECK(env.source->scans == scans);
  CHECK(send(request(token)));
  CHECK((*method)->pump() == runtime::policy::StartResult::Started);
  frame = decoder.consume(transport->frames.back());
  CHECK(frame && schema->validate(frame->message->view()));
  CHECK(frame->message->view().at("result").at("next_cursor").missing());
  auto parameters = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:execution-list:1#/$defs/request/properties/params"})",
      resources);
  CHECK(parameters);
  control::Hello hello{"test.app", context.host, context.host};
  hello.observation_backend = "mock";
  auto router = control::Router::create(
      hello, env.caller, {{"execution.list", *parameters, *method}});
  CHECK(router);
  auto greeting = data::Payload::parse(
      R"({"jsonrpc":"2.0","id":"hello","method":"host.hello","params":{"api_version":"ock.control/1"}})");
  CHECK(greeting && router->dispatch(greeting->view()));
  auto bad_rpc =
      data::Payload::parse("{\"jsonrpc\":\"2.0\",\"id\":\"bad-cursor\","
                           "\"method\":\"execution.list\",\"params\":" +
                           request(tampered) + "}");
  CHECK(bad_rpc);
  auto bad_reply = router->dispatch(bad_rpc->view());
  CHECK(bad_reply);
  auto bad_payload = data::Payload::parse(bad_reply->json);
  CHECK(bad_payload && bad_payload->view().at("error").at("message").string() ==
                           "CursorInvalid");
  auto routed_request = data::Payload::parse(
      R"({"jsonrpc":"2.0","id":"routed-list","method":"execution.list","params":{"page_size":1}})");
  CHECK(routed_request);
  auto routed = router->dispatch(routed_request->view());
  CHECK(routed && routed->queued && routed->json.empty());
  CHECK((*method)->pump() == runtime::policy::StartResult::Started);
  clock->elapsed = 120;
  CHECK(!send(request(token)));
  CHECK(env.source->scans == scans + 2);
  auto before = transport->frames.size();
  CHECK(send(R"({"page_size":1})"));
  auto policy = policy_test::configuration().principals[0];
  policy.rules.clear();
  CHECK(env.assembly.administration->replace_principal_policy(policy));
  CHECK(!(*method)->pump());
  CHECK(transport->frames.size() == before);
  CHECK(control::CursorCodec::cursor_handles() == 0);
  {
    policy_test::Env isolated;
    isolated.source->retention_scope=name("managed_active_and_retained_terminal");
    auto input=isolated.source->rows[0].second.summary->value();input.execution={id<foundation::TaskId>(3)};
    auto third=contracts::ExecutionSummary::create(input);CHECK(third);
    auto access=isolated.source->rows[0].second;access.summary=*third;
    isolated.source->rows.insert(isolated.source->rows.begin(),{3,access});
    auto time=std::make_shared<Clock>();
    auto private_codec=control::CursorCodec::create(context.host,time);CHECK(private_codec);
    auto output=std::make_shared<Transport>();
    auto pages=control::ListMethod::create(isolated.session,isolated.caller,output,*private_codec,context);CHECK(pages);
    auto dispatch=[&](std::string_view json){auto payload=data::Payload::parse(json);CHECK(payload);return (*pages)->dispatch(*isolated.caller,"page",payload->view());};
    auto next_token=[&]{CHECK((*pages)->pump()==runtime::policy::StartResult::Started);control::FrameDecoder d;auto f=d.consume(output->frames.back());CHECK(f&&f->message);return std::string(*f->message->view().at("result").at("next_cursor").string());};
    CHECK(dispatch(R"({"page_size":1})"));auto initial=next_token();
    auto narrow=policy_test::rules();narrow.pop_back();
    auto sibling=isolated.assembly.store->open({{std::byte{7}}},{narrow,isolated.auth->identity.deadline,false});CHECK(sibling);
    auto sibling_caller=(*sibling)->verify({policy_test::principal(),{}, {}});CHECK(sibling_caller);
    auto sibling_method=control::ListMethod::create(*sibling,*sibling_caller,std::make_shared<Transport>(),*private_codec,context);CHECK(sibling_method);
    auto copied=data::Payload::parse(request(initial));CHECK(copied);
    auto scans_before=isolated.source->scans;
    CHECK(!(*sibling_method)->dispatch(**sibling_caller,"cross-session",copied->view()));
    CHECK(isolated.source->scans==scans_before);
    time->elapsed=110;CHECK(dispatch(request(initial)));auto second_cursor=next_token();
    scans_before=isolated.source->scans;time->elapsed=121;
    auto expired=dispatch(request(second_cursor));CHECK(!expired&&expired.error().code().value()==2);
    CHECK(isolated.source->scans==scans_before);
    CHECK(dispatch(R"({"page_size":1})"));auto before_restrict=next_token();
    CHECK(isolated.session->restrict_delegation({narrow,isolated.auth->identity.deadline,false}));
    // 委托变化本就撤销旧 VerifiedCaller；用新可信调用者验证 cursor 自身的视图绑定。
    auto refreshed=isolated.session->verify({policy_test::principal(),{}, {}});CHECK(refreshed);
    (*pages)->disconnect();isolated.caller=*refreshed;output=std::make_shared<Transport>();
    pages=control::ListMethod::create(isolated.session,isolated.caller,output,*private_codec,context);CHECK(pages);
    scans_before=isolated.source->scans;
    CHECK(!dispatch(request(before_restrict)));CHECK(isolated.source->scans==scans_before);
    CHECK(dispatch(R"({"page_size":1})")); // 当前委托仍允许读取，但旧视图不能复用。
    (*pages)->disconnect();(*sibling_method)->disconnect();
  }
  (*method)->disconnect();
  policy_test::Env closing_env;
  closing_env.source->retention_scope=name("managed_active_and_retained_terminal");
  auto closing_transport=std::make_shared<Transport>();
  auto closing=control::ListMethod::create(closing_env.session,closing_env.caller,closing_transport,*codec,context);CHECK(closing);
  closing_transport->on_reserve=[&]{std::thread worker([&]{(*closing)->disconnect();});worker.join();};
  auto closing_request=data::Payload::parse("{}");CHECK(closing_request);
  CHECK(!(*closing)->dispatch(*closing_env.caller,"close",closing_request->view()));
  CHECK(closing_transport->frames.empty() && !(*closing)->pump());
  std::cout << "List authorized frames, stateless continuation, tamper, "
               "filter, TTL and revoke passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
