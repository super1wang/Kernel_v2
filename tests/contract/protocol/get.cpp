#include "tests/contract/authorization/fixtures.hpp"
#include <fstream>
#include <ock/control/get_method.hpp>
using namespace ock;
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
  CHECK(argc == 2);
  std::ifstream input(argv[1]);
  std::string schema_text((std::istreambuf_iterator<char>(input)), {});
  auto schema = binding::CompiledSchema::compile(schema_text);
  CHECK(schema);
  policy_test::Env env;
  auto summary = env.source->rows[0].second.summary->value();
  summary.facts.push_back({id<FactId>(), FactKind::Unknown,
                           Application::NotApplied, id<EffectId>()});
  auto updated = ExecutionSummary::create(summary);
  CHECK(updated);
  env.source->rows[0].second.summary = *updated;
  auto transport = std::make_shared<Transport>();
  auto method = control::GetMethod::create(env.session, env.caller, transport);
  CHECK(method);
  auto parameters = control::GetMethod::parameters();
  CHECK(parameters);
  auto host = control::wire_text(env.source->source_id.host);
  control::Hello hello{"test.app", host, host};
  hello.observation_backend = "mock";
  auto router = control::Router::create(
      hello, env.caller, {{"execution.get", *parameters, *method}});
  CHECK(router);
  auto send = [&](std::string_view json) {
    auto p = data::Payload::parse(json);
    CHECK(p);
    return router->dispatch(p->view());
  };
  CHECK(send(
      R"({"jsonrpc":"2.0","id":"h","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  auto request = [](std::string ref) {
    return "{\"jsonrpc\":\"2.0\",\"id\":\"get\",\"method\":\"execution.get\","
           "\"params\":{\"execution_ref\":{\"execution_id\":\"" +
           ref + "\"}}}";
  };
  auto existing = control::wire_text(summary.execution.execution_id);
  auto reply = send(request(existing));
  CHECK(reply && reply->queued && transport->frames.empty());
  CHECK((*method)->pump() == runtime::policy::StartResult::Started);
  control::FrameDecoder decoder;
  auto decoded = decoder.consume(transport->frames.back());
  CHECK(decoded && decoded->message &&
        schema->validate(decoded->message->view().at("result")));
  auto result = decoded->message->view().at("result");
  CHECK(result.at("observation_version").string() == "1" &&
        result.at("full_result_available").boolean() == false);
  CHECK(result.at("fact_summaries").at(std::size_t{0}).at("kind").string() ==
        "Unknown");
  CHECK(result.at("fact_summaries")
            .at(std::size_t{0})
            .at("application")
            .missing());
  CHECK(result.at("fact_summaries")
            .at(std::size_t{0})
            .at("reference_kind")
            .string() == "Effect");
  summary.phase=ExecutionPhase::Finalizing;summary.version=*ObservationVersion::create(2);
  summary.progress.scope=ResultScope::Candidate;
  summary.evidence=EvidenceState::RequiredRecordFailed;summary.record_state=RequiredRecordState::Failed;
  summary.fault=contracts::error(ContractsErrc::Rejected);summary.writes_blocked=true;summary.repair=RepairKind::ManualReview;
  updated=ExecutionSummary::create(summary);CHECK(updated);env.source->rows[0].second.summary=*updated;
  CHECK(send(request(existing)));CHECK((*method)->pump()==runtime::policy::StartResult::Started);
  control::FrameDecoder failed_decoder;auto failed_frame=failed_decoder.consume(transport->frames.back());CHECK(failed_frame&&failed_frame->message);
  auto failed_summary=failed_frame->message->view().at("result");CHECK(schema->validate(failed_summary));
  CHECK(failed_summary.at("phase").string()=="Finalizing"&&failed_summary.at("evidence").string()=="RequiredRecordFailed");
  CHECK(failed_summary.at("record_state").string()=="Failed"&&failed_summary.at("writes_blocked").boolean()==true);
  CHECK(failed_summary.at("repair").string()=="ManualReview"&&!failed_summary.at("fault").missing());
  CHECK(failed_summary.at("progress").at("scope").string()=="Candidate");
  auto bad = send(request(std::string(32, '0')));
  CHECK(bad);
  auto bad_json = data::Payload::parse(bad->json);
  CHECK(bad_json && bad_json->view().at("error").at("code").int64() == -32602);
  auto missing = send(request(std::string(32, 'f')));
  CHECK(missing);
  auto missing_json = data::Payload::parse(missing->json);
  CHECK(missing_json &&
        missing_json->view().at("error").at("code").int64() == -32010);
  auto before = transport->frames.size();
  CHECK(send(request(existing)));
  auto policy = policy_test::configuration().principals[0];
  policy.rules.clear();
  CHECK(env.assembly.administration->replace_principal_policy(policy));
  CHECK(!(*method)->pump() && transport->frames.size() == before);
  router->close();
  CHECK(!(*method)->pump());
  policy_test::Env closing_env;
  auto closing_transport=std::make_shared<Transport>();
  auto closing=control::GetMethod::create(closing_env.session,closing_env.caller,closing_transport);CHECK(closing);
  closing_transport->on_reserve=[&]{std::thread worker([&]{(*closing)->disconnect();});worker.join();};
  auto closing_request=data::Payload::parse("{\"execution_ref\":{\"execution_id\":\""+control::wire_text(closing_env.source->rows[0].second.summary->value().execution.execution_id)+"\"}}");CHECK(closing_request);
  CHECK(!(*closing)->dispatch(*closing_env.caller,"close",closing_request->view()));
  CHECK(closing_transport->frames.empty() && !(*closing)->pump());
  std::cout << "Get mock summary, Unknown preservation and send-time "
               "revocation passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
