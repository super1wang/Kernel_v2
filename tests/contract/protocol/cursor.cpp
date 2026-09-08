#include <fstream>
#include <iostream>
#include <ock/control/cursor.hpp>
namespace ock::control {
struct CursorTestAccess {
  static auto create(std::string host,
                     std::shared_ptr<const CursorClock> clock) {
    std::array<std::byte, 32> key{};
    for (unsigned i = 0; i < 32; ++i)
      key[i] = std::byte(i);
    return CursorCodec::with_secret(std::move(host), std::move(clock), key);
  }
};
} // namespace ock::control
using namespace ock;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << '\n';                                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)
struct Clock : control::CursorClock {
  std::uint64_t utc = 1000, steady = 100;
  std::uint64_t utc_seconds() const noexcept override { return utc; }
  std::uint64_t monotonic_seconds() const noexcept override { return steady; }
};
int main(int argc, char **argv) {
  CHECK(argc == 2);
  std::ifstream stream(argv[1]);
  std::string text((std::istreambuf_iterator<char>(stream)), {});
  auto golden = data::Payload::parse(text);
  CHECK(golden);
  auto g = golden->view();
  auto context = g.at("context");
  control::CursorContext c{
      std::string(*context.at("host").string()),
      std::string(*context.at("caller").at("principal_id").string()),
      std::string(
          *context.at("filter").at("owner").at("principal_id").string()),
      std::string(*context.at("store").string()),
      1,
      1,
      "nonterminal"};
  auto clock = std::make_shared<Clock>();
  auto codec = control::CursorTestAccess::create(c.host, clock);
  CHECK(codec);
  auto token = codec->issue(c, {10, 8, 1000, 1120});
  CHECK(token && *token == *g.at("token").string());
  auto decoded = codec->read(*token, c);
  CHECK(decoded && decoded->position == 8);
  for (std::size_t i = 3; i < token->size(); i += 17) {
    auto tampered = *token;
    tampered[i] = tampered[i] == 'A' ? 'B' : 'A';
    CHECK(!codec->read(tampered, c));
  }
  for (int change = 0; change < 6; ++change) {
    auto changed = c;
    if (change == 0)
      changed.host[0] = '0';
    if (change == 1)
      changed.caller[0] = '0';
    if (change == 2)
      changed.owner[0] = '0';
    if (change == 3)
      changed.restore = 2;
    if (change == 4)
      changed.view = 2;
    if (change == 5)
      changed.phases = "all";
    CHECK(!codec->read(*token, changed));
  }
  CHECK(!codec->read(*token + "=", c));
  CHECK(!codec->read(std::string(2049, 'a'), c));
  CHECK(!codec->issue(c, {10, 11, 1000, 1120}));
  CHECK(!codec->issue(c, {10, 0, 1000, 1120}));
  clock->utc = 1;
  clock->steady = 220;
  CHECK(!codec->read(*token, c));
  auto restarted = control::CursorCodec::create(c.host, clock);
  CHECK(restarted && !restarted->read(*token, c));
  CHECK(control::CursorCodec::cursor_handles() == 0);
  std::cout << "Cursor golden, MAC tamper, bindings and monotonic expiration "
               "passed\n";
}
