#include <atomic>
#include <iostream>
#include <ock/dynamic/binding/schema.hpp>
#include <thread>
using namespace ock;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "failed at " << __LINE__ << '\n';                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  const std::string text =
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"x":{"$ref":"urn:ock:positive"}},"required":["x"],"additionalProperties":false})";
  binding::SchemaResources resources{
      {"urn:ock:positive", R"({"type":"integer","minimum":1})"}};
  CHECK(!binding::CompiledSchema::compile(text));
  auto schema = binding::CompiledSchema::compile(text, resources);
  CHECK(schema);
  auto same = binding::CompiledSchema::compile(text, resources);
  CHECK(same && same->cache_key().data() == schema->cache_key().data());
  auto good = data::Payload::parse(R"({"x":1})"),
       bad = data::Payload::parse(R"({"x":0})");
  CHECK(good && bad);
  CHECK(schema->validate(good->view()) && !schema->validate(bad->view()));
  std::atomic<bool> ok = true;
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t)
    threads.emplace_back([&] {
      for (int n = 0; n < 100; ++n)
        if (!schema->validate(good->view()) || schema->validate(bad->view()))
          ok = false;
    });
  for (auto &t : threads)
    t.join();
  CHECK(ok);
  resources["urn:ock:positive"] = R"({"type":"integer","minimum":2})";
  auto changed = binding::CompiledSchema::compile(text, resources);
  CHECK(changed && changed->cache_key() != schema->cache_key() &&
        !changed->validate(good->view()));
  CHECK(!binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"https://example.invalid/private"})"));
  auto format = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"string","format":"email"})");
  auto annotation = data::Payload::parse(R"("not an email")");
  CHECK(format && annotation && format->validate(annotation->view()));
  auto complex = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","oneOf":[{"const":1},{"const":2}]})");
  auto one = data::Payload::parse("1"), three = data::Payload::parse("3");
  CHECK(complex && complex->semantics()==binding::SchemaSemantics::DynamicOnly);
  CHECK(complex && complex->validate(one->view()) &&
        !complex->validate(three->view()));
  auto named_default = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","properties":{"default":{"const":{"default":7}}},"required":["default"]})");
  auto literal_default = data::Payload::parse(R"({"default":{"default":7}})");
  CHECK(named_default && literal_default && named_default->validate(literal_default->view()));
  CHECK(!binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","properties":{"x":{"default":7}}})"));
  std::cout << "Compiled schema registration, reference and concurrent "
               "validation passed\n";
}
