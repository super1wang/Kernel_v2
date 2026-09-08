#include <ock/control/cursor.hpp>
#include <ock/control/invoke_method.hpp>
#include <ock/control/list_method.hpp>
#include <ock/control/subscription_methods.hpp>
#include <ock/dynamic/binding/schema.hpp>
#include <ock/dynamic/catalog/catalog.hpp>
#include <iostream>

struct Clock final : ock::control::CursorClock {
  std::uint64_t utc_seconds() const noexcept override { return 1000; }
  std::uint64_t monotonic_seconds() const noexcept override { return 100; }
};

int main() {
  auto value = ock::data::Payload::parse(R"({"x":7})");
  auto schema = ock::binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"x":{"type":"integer"}},"required":["x"],"additionalProperties":false})");
  if (!value || !schema || !schema->validate(value->view())) return 1;
  auto frame = ock::control::encode_frame(*value);
  if (!frame) return 2;
  ock::control::FrameDecoder decoder;
  auto decoded = decoder.consume(*frame);
  if (!decoded || !decoded->message || !decoder.finish()) return 3;
  std::string host(32, '1');
  auto codec = ock::control::CursorCodec::create(host, std::make_shared<Clock>());
  if (!codec) return 4;
  ock::control::CursorContext context{host, std::string(32, '2'), std::string(32, '3')};
  context.view = 1;
  auto token = codec->issue(context, 10, 3);
  if (!token) return 5;
  auto position = codec->read(*token, context);
  if (!position || position->position != 3) return 6;
  token->back() = token->back() == 'a' ? 'b' : 'a';
  if (codec->read(*token, context)) return 7;
  std::cout << "installed B2 Data/Schema/frame/cursor passed\n";
}
