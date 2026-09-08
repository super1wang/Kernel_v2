#include <iostream>
#include <ock/control_protocol/observation_wire.hpp>
using namespace ock;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << '\n';                                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  contracts::PrincipalRef caller{};
  caller.principal_id.bytes[15] = 1;
  auto parse = [](std::string_view text) { return data::Payload::parse(text); };
  auto input = parse(
      R"({"filter":{"owner":"self"},"topics":["execution.progress"],"min_interval_ms":0})");
  auto request = control::parse_subscribe(input->view(), caller);
  CHECK(request && request->filter.owner == caller &&
        request->interval.count() == 50);
  for (
      auto text :
      {R"({"filter":{"owner":"self","executions":[]},"topics":["execution.phase"]})",
       R"({"filter":{"owner":"self"},"topics":["other"]})",
       R"({"filter":{"executions":[{"execution_id":"00000000000000000000000000000001"},{"execution_id":"00000000000000000000000000000001"}]},"topics":["execution.phase"]})",
       R"({"filter":{"owner":"self"},"topics":["execution.phase"],"min_interval_ms":2147483648})",
       R"({"filter":{"owner":"self"},"topics":["execution.phase"],"unexpected":true})"}) {
    input = parse(text);
    CHECK(input && !control::parse_subscribe(input->view(), caller));
  }
  input = parse("{}");
  auto list = control::parse_list(input->view(), caller);
  CHECK(list && list->request.owner == caller &&
        list->request.budget.page_size == 50 &&
        list->request.budget.scan_limit == 2000);
  for (auto text :
       {R"({"page_size":0})", R"({"page_size":201})",
        R"({"phase_set":"Finalizing"})", R"({"owner":"someone"})",
        R"({"cursor":""})", R"({"cursor":"v1.a."})", R"({"limit":1})"}) {
    input = parse(text);
    CHECK(input && !control::parse_list(input->view(), caller));
  }
  input = parse(
      R"({"subscription_id":"00000000000000000000000000000001","stream_generation":"00000000000000000000000000000002"})");
  CHECK(control::parse_unsubscribe(input->view()));
  input = parse(
      R"({"subscription_id":"0000000000000000000000000000000A","stream_generation":"00000000000000000000000000000002"})");
  CHECK(!control::parse_unsubscribe(input->view()));
  auto max = parse(R"("18446744073709551615")");
  CHECK(control::wire_count(max->view()) == UINT64_MAX);
  auto overflow = parse(R"("18446744073709551616")");
  CHECK(!control::wire_count(overflow->view()));
  auto leading = parse(R"("01")");
  CHECK(!control::wire_count(leading->view()));
  std::cout
      << "Observation wire identity, filter, interval and list bounds passed\n";
}
