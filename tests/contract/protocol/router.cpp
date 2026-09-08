#include "tests/contract/authorization/fixtures.hpp"
#include <ock/control/router.hpp>
using namespace ock;
struct Invoke final : control::MethodPort {
  unsigned calls = 0;
  bool throw_now = false;
  Result<data::Payload> call(const runtime::policy::VerifiedCaller &,
                             data::ValueView) override {
    ++calls;
    if (throw_now)
      throw std::runtime_error("transport boundary test");
    return data::Payload::parse(
        R"({"status":"Indeterminate","facts":[{"kind":"Unknown","reference":"effect-1"}]})");
  }
};
int main() try {
  policy_test::Env env;
  auto invoke = std::make_shared<Invoke>();
  auto schema = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","additionalProperties":false})");
  CHECK(schema);
  control::Hello identity{"test.app", "00000000000000000000000000000001",
                          "00000000000000000000000000000001"};
  CHECK(!control::Router::create({"test.app", "invalid", "invalid"}, env.caller,
                                 {{"operation.invoke", *schema, invoke}}));
  auto router = control::Router::create(
      identity, env.caller, {{"operation.invoke", *schema, invoke}});
  CHECK(router);
  auto send = [&](std::string_view text) {
    auto input = data::Payload::parse(text);
    CHECK(input);
    return router->dispatch(input->view());
  };
  auto early = send(
      R"({"jsonrpc":"2.0","id":"0","method":"operation.invoke","params":{}})");
  CHECK(early && early->close && invoke->calls == 0);
  router = control::Router::create(identity, env.caller,
                                   {{"operation.invoke", *schema, invoke}});
  CHECK(router);
  auto hello = send(
      R"({"jsonrpc":"2.0","id":"1","method":"host.hello","params":{"api_version":"ock.control/1"}})");
  CHECK(hello && !hello->close);
  auto parsed = data::Payload::parse(hello->json);
  CHECK(parsed &&
        parsed->view().at("result").at("supported_methods").size() == 2);
  auto response = send(
      R"({"jsonrpc":"2.0","id":"quote\"id","method":"operation.invoke","params":{}})");
  CHECK(response && invoke->calls == 1);
  parsed = data::Payload::parse(response->json);
  CHECK(parsed && parsed->view().at("error").missing());
  CHECK(parsed->view().at("result").at("status").string() == "Indeterminate");
  CHECK(parsed->view().at("result").at("facts").size() == 1);
  auto bad = send(
      R"({"jsonrpc":"2.0","id":"3","method":"operation.invoke","params":{"extra":1}})");
  CHECK(bad && invoke->calls == 1);
  parsed = data::Payload::parse(bad->json);
  CHECK(parsed->view().at("error").at("code").int64() == -32602);
  auto unknown =
      send(R"({"jsonrpc":"2.0","id":"4","method":"plan.submit","params":{}})");
  CHECK(unknown && invoke->calls == 1);
  parsed = data::Payload::parse(unknown->json);
  CHECK(parsed->view().at("error").at("code").int64() == -32601);
  auto wrong =
      send(R"({"jsonrpc":"2.0","method":"notifications.event","params":{}})");
  CHECK(wrong && wrong->close && wrong->json.empty());
  CHECK(!send(
      R"({"jsonrpc":"2.0","id":"5","method":"operation.invoke","params":{}})"));
  router = control::Router::create(identity, env.caller,
                                   {{"operation.invoke", *schema, invoke}});
  CHECK(router);
  CHECK(send(
      R"({"jsonrpc":"2.0","id":"6","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  invoke->throw_now = true;
  auto thrown = send(
      R"({"jsonrpc":"2.0","id":"7","method":"operation.invoke","params":{}})");
  CHECK(thrown && thrown->close && thrown->json.empty());
  std::cout << "Router handshake, direction, schema and business result "
               "envelope passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
