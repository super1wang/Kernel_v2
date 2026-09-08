#include <cmath>
#include <crtdbg.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ock/data/payload.hpp>
#include <thread>
#define NOMINMAX
#include <Windows.h>

#include <DbgHelp.h>
using namespace ock::data;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " << #x << '\n';                             \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static_assert(!std::is_copy_constructible_v<Payload>);
static_assert(std::is_copy_constructible_v<SharedPayload>);
template <class T>
concept TemporaryView = requires(T t) { std::move(t).view(); };
static_assert(!TemporaryView<Payload> && !TemporaryView<SharedPayload>);
int main(int argc, char **argv) {
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  std::set_terminate([] {
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    void *frames[32];
    auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (unsigned i = 0; i < count; ++i) {
      alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512]{};
      auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 511;
      DWORD64 displacement = 0;
      if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]),
                      &displacement, symbol))
        std::cerr << symbol->Name << '\n';
    }
    std::abort();
  });
  if (argc != 2)
    return 2;
  const std::string test = argv[1];
  if (test == "values") {
    auto p = Payload::parse(
        R"({"n":null,"u":18446744073709551615,"i":-9223372036854775808,"a":[true,"x"]})");
    CHECK(p);
    auto v = p->view();
    CHECK(v.at("absent").missing());
    CHECK(v.at("n").kind() == Kind::Null);
    CHECK(v.at("u").uint64().value() == UINT64_MAX);
    CHECK(!v.at("u").int64());
    CHECK(!v.at("u").number());
    CHECK(v.at("i").int64().value() == INT64_MIN);
    CHECK(!v.at("i").uint64());
    CHECK(v.at("a").at(0).boolean().value());
    CHECK(v.at("a").at(1).string().value() == "x");
  } else if (test == "duplicate") {
    CHECK(!Payload::parse(R"({"a":1,"\u0061":2})"));
    CHECK(!Payload::parse(R"({"x":{"a":1,"a":2}})"));
    CHECK(Payload::parse(R"({"a":1,"x":{"a":2}})"));
  } else if (test == "invalid") {
    for (auto s : {"NaN", "Infinity", "1e9999", "18446744073709551616", "{}{}",
                   "[1,]", "{\"a\":1,}", "/*x*/{}"}) {
      CHECK(!Payload::parse(s));
    }
    CHECK(!Payload::parse(std::string("\"\xff\"", 3)));
    const std::string surrogate = "\"\\ud800\"";
    CHECK(!Payload::parse(surrogate));
  } else if (test == "budgets") {
    std::cerr << "frame\n";
    Budget b;
    b.frame_bytes = 2;
    CHECK(!Payload::parse("[1]", b));
    std::cerr << "token\n";
    b = {};
    b.token_bytes = 3;
    CHECK(!Payload::parse("1234", b));
    std::cerr << "depth\n";
    b = {};
    b.depth = 1;
    CHECK(!Payload::parse("[[]]", b));
    CHECK(Payload::parse("[]", b));
    std::cerr << "nodes\n";
    b = {};
    b.nodes = 2;
    CHECK(!Payload::parse("[1,2]", b));
    std::cerr << "container\n";
    b = {};
    b.container_items = 1;
    CHECK(!Payload::parse("[1,2]", b));
    std::cerr << "text\n";
    b = {};
    b.text_bytes = 2;
    CHECK(!Payload::parse(R"(["aa","b"])", b));
    std::cerr << "allocation minimum\n";
    b = {};
    b.allocation_bytes = 1;
    CHECK(!Payload::parse("null", b));
    std::cerr << "allocation boundary\n";
    auto ok = Payload::parse(R"({"a":[1,2,3]})");
    CHECK(ok);
    b = {};
    b.allocation_bytes = ok->allocated_bytes() - 1;
    CHECK(!Payload::parse(R"({"a":[1,2,3]})", b));
  } else if (test == "freeze") {
    PayloadBuilder b;
    CHECK(b.begin_object());
    CHECK(b.key("x"));
    CHECK(b.uint64(7));
    CHECK(b.end_object());
    auto p = b.freeze();
    CHECK(p);
    CHECK(!b.key("y"));
    CHECK(!b.freeze());
    CHECK(p->view().at("x").uint64().value() == 7);
    PayloadBuilder bad;
    CHECK(bad.begin_object());
    CHECK(bad.key("x"));
    CHECK(!bad.end_object());
    CHECK(!bad.freeze());
    PayloadBuilder dup;
    CHECK(dup.begin_object());
    CHECK(dup.key("a"));
    CHECK(dup.null());
    CHECK(!dup.key("a"));
    CHECK(!dup.freeze());
    Budget tiny;
    tiny.allocation_bytes = 1;
    PayloadBuilder tiny_builder(tiny);
    CHECK(!tiny_builder.null());
    CHECK(!tiny_builder.freeze());
  } else if (test == "allocation_failures") {
    for (const std::string sample :
         {std::string(R"({"a":[1,2,3],"b":{"k":"value"}})"),
          std::string("[\"") + std::string(5000, 'x') + "\"]"}) {
      auto ok = Payload::parse(sample);
      CHECK(ok);
      const auto used = ok->allocated_bytes();
      for (std::size_t cap = 0; cap < used;
           cap += (std::max)(std::size_t(1), used / 64)) {
        Budget b;
        b.allocation_bytes = cap;
        auto result = Payload::parse(sample, b);
        CHECK(!result);
        CHECK(result.error().code() == error(DataErrc::BudgetExceeded).code());
      }
      Budget exact;
      exact.allocation_bytes = used;
      CHECK(Payload::parse(sample, exact));
    }
  } else if (test == "ownership") {
    auto p = Payload::parse(R"({"x":["hello"]})");
    CHECK(p);
    auto clone = p->clone();
    CHECK(clone);
    auto borrowed = p->view().at("x");
    auto shared = std::move(*p).share();
    CHECK(p->view().missing());
    auto second = shared;
    CHECK(borrowed.at(0).string().value() == "hello");
    CHECK(second.view().at("x").at(0).string().value() == "hello");
    CHECK(clone->view().at("x").at(0).string().value().data() !=
          borrowed.at(0).string().value().data());
  } else if (test == "builder_numbers") {
    PayloadBuilder p;
    CHECK(!p.number(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!p.freeze());
    PayloadBuilder b;
    CHECK(b.begin_array());
    CHECK(b.number(1.5));
    CHECK(b.end_array());
    auto v = b.freeze();
    CHECK(v);
    CHECK(v->view().at(0).number().value() == 1.5);
  } else if (test == "encode") {
    auto p = Payload::parse(R"({"a":"\u0000","b":18446744073709551615})");
    CHECK(p);
    auto encoded = p->encode();
    CHECK(encoded);
    CHECK(Payload::parse(*encoded));
    CHECK(!p->encode(2));
  } else if (test == "concurrent") {
    bool ok[4] = {};
    std::thread ts[4];
    for (int i = 0; i < 4; ++i)
      ts[i] = std::thread([&, i] {
        for (int k = 0; k < 50; ++k) {
          auto p = Payload::parse("[1,2,3]");
          if (!p)
            return;
        }
        ok[i] = true;
      });
    for (auto &t : ts)
      t.join();
    for (bool v : ok)
      CHECK(v);
  } else if (test == "buffer") {
    std::byte raw[]{std::byte{1}, std::byte{2}};
    CHECK(!Buffer::copy(raw, 1));
    auto b = Buffer::copy(raw, 2);
    CHECK(b);
    raw[0] = std::byte{3};
    CHECK(b->bytes()[0] == std::byte{1});
  } else if (test == "growth_cleanup") {
    std::string wide = "{";
    for (int i = 0; i < 1200; ++i) {
      if (i)
        wide += ',';
      wide += '"' + std::to_string(i) + "\":[1,2,3]";
    }
    wide += '}';
    auto p = Payload::parse(wide);
    CHECK(p);
    CHECK(p->view().size() == 1200);
    auto nested =
        Payload::parse(std::string(64, '[') + "0" + std::string(64, ']'));
    CHECK(nested);
    CHECK(!Payload::parse(std::string(65, '[') + "0" + std::string(65, ']')));
  } else if (test == "escape") {
    auto p = Payload::parse("\"" + std::string(5000, 'x') + "\"");
    CHECK(p);
    auto borrow = p->view().string().value();
    *p = Payload();
    volatile char value = borrow[100];
    return value;
  } else
    return 2;
  return 0;
}
