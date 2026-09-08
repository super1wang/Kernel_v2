#include "tests/contract/native/fixtures.hpp"
#include <crtdbg.h>
#include <ock/dynamic/binding/bound_operation.hpp>
#include <ock/control/invoke_method.hpp>
using namespace ock;
struct Value {
  std::int64_t amount{};
};
struct Fields {
  static contracts::TypeIdentity identity() {
    return {name("binding.value"), ver(), {}};
  }
  static auto fields() {
    auto f = binding::field<&Value::amount>("amount");
    f.minimum = 0;
    f.maximum = 100;
    return std::tuple(f);
  }
};
namespace ock::contracts {
template <>
struct TypeContract<Value> : binding::TypeContract<Value, Fields> {};
} // namespace ock::contracts
static unsigned calls = 0;
struct Lifecycle final : runtime::host::ModuleLifecyclePort {
  Result<void> start(const runtime::host::ModuleContext &) override {
    return {};
  }
  runtime::host::ModuleStopResult stop() override { return {true, {}}; }
};
static Result<Value> compute(const Value &v, contracts::WorkContext &) {
  ++calls;
  return Value{v.amount + 1};
}
static Result<std::size_t>
targets(const Value &, std::span<foundation::ObjectId> out) noexcept {
  if (out.empty())
    return foundation::make_unexpected(
        contracts::error(contracts::ContractsErrc::BudgetExceeded));
  out[0] = policy_test::target();
  return 1;
}
int main() try {
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  using namespace runtime;
  auto clock = std::make_shared<policy_test::Clock>();
  auto auth =
      std::make_shared<policy_test::Auth>(clock->now() + std::chrono::hours(1));
  auto threads = std::make_shared<native_test::Threads>();
  auto host = host::NativeHost::create(
      {}, policy_test::configuration(),
      {auth, clock, std::make_shared<policy_test::Digest>(), threads, {}});
  CHECK(host);
  struct StopHost {
    runtime::host::NativeHost &value;
    ~StopHost() {
      value.shutdown_until(std::chrono::steady_clock::now() +
                           std::chrono::seconds(1));
    }
  } stop{**host};
  registry::ModuleManifest manifest{name("native"), ver()};
  manifest.operations.push_back(key());
  manifest.executors.push_back(name("test"));
  registry::ModuleInput module{manifest,
                               {},
                               {},
                               {},
                               {},
                               {{name("test"), name("app"), false, false,
                                 std::make_shared<native_test::Executor>()}},
                               {}};
  module.register_operations = [](registry::Registrar &registrar) {
    DefinitionInput definition{key(),
                               {},
                               {true, false, false, name("test"), name("app")},
                               AtomicMode::PureCompute,
                               {name("allow")},
                               "binding"};
    registry::OperationOptions options{
        {}, {}, {name("native"), name("test")}, {}, false};
    CHECK(registrar.compute(compute, definition, options));
  };
  CHECK((*host)->add({std::move(module), std::make_shared<Lifecycle>()}));
  CHECK((*host)->start());
  auto session = (*host)->open(
      {{std::byte{7}}}, {policy_test::rules(), auth->identity.deadline, false});
  CHECK(session);
  auto caller = session->verify({policy_test::principal(), {}, {}});
  CHECK(caller);
  std::array selected{policy_test::target()};
  auto dynamic = binding::BoundOperation<Value, Value>::create(
      *session, key(), {}, Shape::Read, *caller, selected, targets,
      name("binding.dynamic"));
  CHECK(dynamic);
  auto native =
      session->bind<Value, Value>(key(), {}, Shape::Read, *caller, selected,
                                  targets, name("binding.native"));
  CHECK(native);
  invocation::InvokeOptions options{
      {}, clock->now() + std::chrono::seconds(1), 100};
  auto input = data::Payload::parse(R"({"amount":4})");
  CHECK(input);
  auto dynamic_reply = dynamic->invoke(input->view(), options);
  auto native_reply = native->invoke(Value{4}, options);
  auto result = [](const InvokeReply<Value> &reply) {
    CHECK(std::holds_alternative<Completed<Value>>(reply));
    return std::get<ReadCompleted<Value>>(
               std::get<Completed<Value>>(reply).outcome.value())
        .result->amount;
  };
  CHECK(result(dynamic_reply) == 5 && result(native_reply) == 5 && calls == 2);
  for (auto text : {R"({"amount":-1})", R"({"amount":1,"extra":0})",
                    R"({"amount":null})"}) {
    auto bad = data::Payload::parse(text);
    CHECK(bad);
    CHECK(std::holds_alternative<Rejected>(
        dynamic->invoke(bad->view(), options)));
  }
  CHECK(calls == 2);
  auto output_invalid = data::Payload::parse(R"({"amount":100})");
  CHECK(output_invalid);
  auto output_reply = dynamic->invoke(output_invalid->view(), options);
  CHECK(std::holds_alternative<Completed<Value>>(output_reply));
  const auto &output_outcome=std::get<Completed<Value>>(output_reply).outcome;
  CHECK(std::holds_alternative<FailedBeforeApply>(output_outcome.value()));
  auto native_bad=native->invoke(Value{100},options);
  CHECK(std::holds_alternative<Completed<Value>>(native_bad));
  const auto &native_bad_outcome=std::get<Completed<Value>>(native_bad).outcome;
  CHECK(std::holds_alternative<FailedBeforeApply>(native_bad_outcome.value()));
  CHECK(std::get<FailedBeforeApply>(output_outcome.value()).reason.code()==
        std::get<FailedBeforeApply>(native_bad_outcome.value()).reason.code());
  CHECK(calls == 4);
  auto entry=control::InvocationBinding::bind<Value,Value>(*session,key(),{},Shape::Read,*caller,selected,targets,name("binding.rpc"));CHECK(entry);
  auto method=control::InvokeMethod::create({*entry},clock);CHECK(method);
  auto host_id=control::wire_text((*host)->incarnation());
  auto router=control::Router::create({"test.app",host_id,host_id},*caller,{*method});CHECK(router);
  auto send=[&](std::string_view text){auto p=data::Payload::parse(text);CHECK(p);return router->dispatch(p->view());};
  CHECK(send(R"({"jsonrpc":"2.0","id":"hello","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  auto request=[](int amount,std::string digest=std::string(64,'0')){
    return "{\"jsonrpc\":\"2.0\",\"id\":\"invoke\",\"method\":\"operation.invoke\",\"params\":{\"operation\":{\"name\":\"test.read\",\"version\":\"1.0.0\"},\"contract_digest\":\""+digest+"\",\"args\":{\"amount\":"+std::to_string(amount)+"}}}";
  };
  auto response=send(request(4));CHECK(response && !response->close);
  auto response_json=data::Payload::parse(response->json);CHECK(response_json);
  CHECK(response_json->view().at("result").at("kind").string()=="Completed");
  CHECK(response_json->view().at("result").at("outcome").at("result").at("amount").int64()==5);
  auto invalid_args=send(request(-1));CHECK(invalid_args);
  response_json=data::Payload::parse(invalid_args->json);CHECK(response_json);
  CHECK(response_json->view().at("result").at("kind").string()=="Rejected");
  auto stale=send(request(4,std::string(64,'1')));CHECK(stale);
  response_json=data::Payload::parse(stale->json);CHECK(response_json);
  CHECK(response_json->view().at("result").at("kind").string()=="Rejected");
  CHECK(calls==5);
  auto invalid_result=send(request(100));CHECK(invalid_result);
  response_json=data::Payload::parse(invalid_result->json);CHECK(response_json);
  CHECK(response_json->view().at("result").at("kind").string()=="Completed");
  CHECK(response_json->view().at("result").at("outcome").at("kind").string()=="FailedBeforeApply");
  CHECK(calls==6);
  threads->allow = false;
  CHECK(std::holds_alternative<Rejected>(
      dynamic->invoke(input->view(), options)));
  CHECK(calls == 6);
  threads->allow = true;
  CHECK(session->close());
  CHECK(std::holds_alternative<Rejected>(
      dynamic->invoke(input->view(), options)));
  CHECK(calls == 6);
  std::cout << "Dynamic and Native shared TypeContract and governed HostBound "
               "passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
