#include "probe_common.hpp"
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#include <future>
#include <vector>
int main() {
  using jsoncons::json;
  auto value = json::parse(R"({"n":7,"label":"模型"})");
  auto schema = jsoncons::jsonschema::make_json_schema(json::parse(R"({"type":"object","required":["n"],"properties":{"n":{"type":"integer","minimum":1}}})"));
  PROBE_CHECK(schema.is_valid(value)); PROBE_CHECK(!schema.is_valid(json::parse(R"({"n":0})")));
  auto f = std::async(std::launch::async, [&] { return schema.is_valid(value); });
  PROBE_CHECK(f.get());
  std::vector<uint8_t> bytes; jsoncons::cbor::encode_cbor(value, bytes);
  PROBE_CHECK(jsoncons::cbor::decode_cbor<json>(bytes) == value);
  auto duplicate = json::parse(R"({"n":1,"n":2})");
  std::cout << "ordinary CBOR roundtrip only; duplicate-key upstream projection=" << duplicate << "\n";
  // 重复键政策、预算与 canonical CBOR 均需内核合同后续验证；此处不冒充完成。
}
