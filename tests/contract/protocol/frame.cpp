#include <iostream>
#include <ock/control_protocol/protocol.hpp>
using namespace ock;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << '\n';                                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  for (auto id : {std::string(97, 'a'), std::string("中文")}) {
    auto message = data::Payload::parse("{\"jsonrpc\":\"2.0\",\"id\":\"" + id +
                                        "\",\"method\":\"host.hello\"}");
    CHECK(message && !control::request(message->view()));
  }
  auto payload = data::Payload::parse(
      R"({"jsonrpc":"2.0","id":"1","method":"host.hello","params":{}})");
  CHECK(payload);
  auto frame = control::encode_frame(*payload);
  CHECK(frame && frame->size() > 12);
  control::FrameDecoder decoder;
  control::FrameDecoder truncated;
  CHECK(truncated.consume(std::span(*frame).first(11)));
  CHECK(!truncated.finish());
  for (std::size_t i = 0; i < frame->size(); ++i) {
    auto part = decoder.consume(std::span(*frame).subspan(i, 1));
    CHECK(part && part->consumed == 1);
    if (i + 1 == frame->size()) {
      CHECK(part->message);
      CHECK(control::request(part->message->view()));
    } else
      CHECK(!part->message);
  }
  for (std::size_t at : {0, 5, 7}) {
    auto broken = *frame;
    broken[at] = std::byte{0xff};
    control::FrameDecoder bad;
    CHECK(!bad.consume(broken));
    CHECK(!bad.consume(*frame));
  }
  auto badlength = *frame;
  for (std::size_t i = 8; i < 12; ++i)
    badlength[i] = std::byte{0xff};
  control::FrameDecoder big;
  CHECK(!big.consume(badlength));
  auto twice = *frame;
  twice.insert(twice.end(), frame->begin(), frame->end());
  control::FrameDecoder multiple;
  auto first = multiple.consume(twice);
  CHECK(first && first->consumed == frame->size());
  CHECK(multiple.consume(std::span(twice).subspan(first->consumed))->message);
  for (auto json :
       {R"([])", R"({"jsonrpc":"2.0","id":1,"method":"host.hello"})",
        R"({"jsonrpc":"2.0","method":"operation.invoke"})",
        R"({"jsonrpc":"2.0","method":"notifications.event","params":{}})",
        R"({"jsonrpc":"2.0","id":"1","method":"x","params":[]})"}) {
    auto p = data::Payload::parse(json);
    CHECK(p && !control::request(p->view()));
  }
  std::cout
      << "OCK1 partial reads, invalid headers and request directions passed\n";
}
