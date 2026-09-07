// D1.01 固定合同用例；所有检查在 Release 中同样执行。
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <ock/foundation/foundation.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <malloc.h>
#endif
using namespace ock::foundation;
namespace allocation {
std::atomic<bool> enabled{false}, fail{false};
std::atomic<unsigned long long> count{0};
void before() {
  if (fail.load())
    throw std::bad_alloc{};
  if (enabled.load())
    ++count;
}
void start() {
  count = 0;
  enabled = true;
}
unsigned long long stop() {
  enabled = false;
  return count.load();
}
} // namespace allocation
void *operator new(std::size_t n) {
  allocation::before();
  if (auto p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, std::align_val_t alignment) {
  allocation::before();
#ifdef _MSC_VER
  auto p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(alignment));
#else
  auto a = static_cast<std::size_t>(alignment);
  auto p = std::aligned_alloc(a, ((n ? n : 1) + a - 1) / a * a);
#endif
  if (p)
    return p;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t n, std::align_val_t a) {
  return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _MSC_VER
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept {
  ::operator delete(p, a);
}
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept {
  ::operator delete(p, a);
}
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept {
  ::operator delete(p, a);
}
#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return false;                                                            \
    }                                                                          \
  } while (false)
inline constexpr ErrorDomain test_domain{"foundation-test"};
Error sample_error() { return Error{ErrorCode::make<test_domain>(42)}; }
bool code_only_no_allocation() {
  allocation::start();
  void *plain = ::operator new(19);
  void *array = ::operator new[](23);
  void *aligned = ::operator new(64, std::align_val_t{64});
  ::operator delete(plain);
  ::operator delete[](array);
  ::operator delete(aligned, std::align_val_t{64});
  auto control = allocation::stop();
  CHECK(control == 3);
  allocation::start();
  auto code = ErrorCode::make<test_domain>(42);
  auto copied = code;
  Error error{code};
  Error other = error;
  Result<int> success{5};
  auto success_copy = success;
  Result<int> failure = make_unexpected(error);
  auto failed_copy = failure;
  Result<void> ok;
  Result<void> no = make_unexpected(error);
  auto no_copy = no;
  auto propagated = failure.and_then([](int) -> Result<int> { return 1; });
  auto count = allocation::stop();
  CHECK(count == 0);
  CHECK(code == copied && other.code() == code);
  CHECK(success_copy.value() == 5 && !failed_copy && ok && !no_copy &&
        !propagated);
  std::printf("allocation_window=code_only control=%llu observed=%llu\n",
              control, count);
  return true;
}
bool value_operations_no_allocation() {
  constexpr std::string_view text = "00112233445566778899aabbccddeeff";
  allocation::start();
  auto name = Name::parse("Read!A");
  auto copy = name;
  auto id = parse_id<TaskTag>(text);
  auto encoded = encode_id(*id);
  BoundHandle<TaskTag> one{RegistryId{{1}}, RegistryGeneration{{2}}, 0};
  auto two = one;
  bool equal = one == two;
  auto count = allocation::stop();
  CHECK(count == 0 && name && copy && id && equal);
  CHECK(std::string_view(encoded.data(), encoded.size()) == text);
  std::printf("allocation_window=value_operations observed=%llu\n", count);
  return true;
}
bool layout_statistics() {
#define LAYOUT(T) std::printf(#T " size=%zu align=%zu\n", sizeof(T), alignof(T))
  LAYOUT(ErrorCode);
  LAYOUT(ErrorInfo);
  LAYOUT(Error);
  LAYOUT(Result<int>);
  LAYOUT(Result<void>);
  LAYOUT(Name);
  LAYOUT(TaskId);
  LAYOUT(BoundHandle<TaskTag>);
  CHECK(sizeof(TaskId) == 16);
  return true;
}
bool result_move_only_value() {
  static_assert(!std::is_copy_constructible_v<Result<std::unique_ptr<int>>>);
  Result<std::unique_ptr<int>> value{std::make_unique<int>(17)};
  auto moved = std::move(value);
  CHECK(moved && **moved == 17);
  return true;
}
bool result_move_only_error() {
  using R = Result<int, std::unique_ptr<int>>;
  static_assert(!std::is_copy_constructible_v<R>);
  R value = make_unexpected(std::make_unique<int>(19));
  auto moved = std::move(value);
  CHECK(!moved && *moved.error() == 19);
  return true;
}
bool result_void() {
  Result<void> yes;
  Result<void> no = make_unexpected(sample_error());
  auto copy = no;
  CHECK(yes && !no && !copy);
  CHECK(copy.error().code() == sample_error().code());
  CHECK(yes.has_value());
  return true;
}
bool result_error_propagation() {
  auto detail = Error::with_details(sample_error().code(), "owned detail");
  CHECK(detail);
  Result<int> value{3};
  int calls = 0;
  auto yes = value.and_then([&](int n) -> Result<int> {
    ++calls;
    return n + 1;
  });
  CHECK(yes && *yes == 4 && calls == 1);
  Result<int> error = make_unexpected(*detail);
  auto no = error.and_then([&](int) -> Result<int> {
    ++calls;
    return 7;
  });
  CHECK(!no && calls == 1);
  CHECK(no.error().code() == sample_error().code() &&
        no.error().info() == detail->info());
  return true;
}
bool result_bad_access() {
  Result<int> no = make_unexpected(sample_error());
  bool caught = false;
  try {
    static_cast<void>(no.value());
  } catch (const tl::bad_expected_access<Error> &e) {
    caught = e.error().code() == sample_error().code();
  }
  CHECK(caught);
  Result<int> yes{9};
  CHECK(yes.value() == 9);
  return true;
}
struct Throwing {
  explicit Throwing(int) { throw std::runtime_error("user exception"); }
};
bool exception_not_business_failure() {
  bool user = false;
  try {
    Result<Throwing> value{tl::in_place, 1};
  } catch (const std::runtime_error &e) {
    user = std::string_view(e.what()) == "user exception";
  }
  CHECK(user);
  bool oom = false;
  allocation::fail = true;
  try {
    auto value = Error::with_details(
        sample_error().code(),
        "OOM diagnostic content beyond short string storage");
  } catch (const std::bad_alloc &) {
    oom = true;
  } catch (...) {
    allocation::fail = false;
    throw;
  }
  allocation::fail = false;
  CHECK(oom);
  return true;
}
bool static_error_domain() {
  constexpr auto code = ErrorCode::make<test_domain>(42);
  static_assert(code.value() == 42);
  CHECK(code.domain().name() == "foundation-test");
  CHECK(code == sample_error().code());
  return true;
}
bool error_details_ownership() {
  CHECK(!std::is_copy_constructible_v<ErrorInfo>);
  CHECK(!std::is_move_constructible_v<ErrorInfo>);
  CHECK(!std::is_copy_assignable_v<ErrorInfo>);
  CHECK(!std::is_move_assignable_v<ErrorInfo>);
  Error saved = sample_error();
  {
    std::string source = "owned original";
    auto value = Error::with_details(sample_error().code(), source);
    CHECK(value);
    saved = *value;
    source.assign("destroyed");
  }
  CHECK(saved.info() && saved.info()->text() == "owned original");
  auto copied = saved;
  auto moved = std::move(copied);
  CHECK(moved.info() == saved.info());
  allocation::start();
  Error empty = sample_error();
  auto empty_text = Error::with_details(sample_error().code(), "");
  auto empty_info = ErrorInfo::create("");
  auto count = allocation::stop();
  CHECK(!empty.info() && empty_text && !empty_text->info());
  CHECK(empty_info && !*empty_info && count == 0);
  return true;
}
bool error_details_budget() {
  std::string boundary(ErrorInfo::max_bytes, 'x');
  auto accepted = Error::with_details(sample_error().code(), boundary);
  CHECK(accepted && accepted->info()->text().size() == 4096);
  boundary += 'x';
  allocation::start();
  auto rejected = Error::with_details(sample_error().code(), boundary);
  auto count = allocation::stop();
  CHECK(!rejected && count == 0);
  CHECK(rejected.error().code() ==
        make_error(FoundationErrc::detail_too_large).code());
  std::string utf8;
  for (int i = 0; i < 1366; ++i)
    utf8 += "中";
  CHECK(utf8.size() > 4096 &&
        !Error::with_details(sample_error().code(), utf8));
  return true;
}
bool display_utf8() {
  for (auto text : {"", "ASCII", "中文", "\xF0\x9F\x98\x80"})
    CHECK(Error::with_details(sample_error().code(), text));
  for (auto text : {"\xC2", "\xE4\xB8", "\x80", "\xC0\xAF", "\xED\xA0\x80",
                    "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xE2\x28\xA1"})
    CHECK(!Error::with_details(sample_error().code(), text));
  return true;
}
bool checked_arithmetic() {
  using T = std::uint64_t;
  auto max = std::numeric_limits<T>::max();
  CHECK(*checked_add<T>(max - 1, 1) == max);
  CHECK(*checked_mul<T>(max, 0) == 0);
  CHECK(*checked_mul<T>(max, 1) == max);
  CHECK(*checked_sub<T>(max, max) == 0);
  CHECK(!checked_add<T>(max, 1));
  CHECK(!checked_mul<T>(max, 2));
  CHECK(!checked_sub<T>(0, 1));
  CHECK(*checked_mul<std::uint8_t>(15, 17) == 255);
  return true;
}
bool checked_narrowing() {
  static_assert(!UnsignedInteger<bool>);
  static_assert(UnsignedInteger<unsigned>);
  CHECK(*checked_narrow<std::uint8_t>(std::uint64_t{255}) == 255);
  CHECK(*checked_narrow<std::uint8_t>(0u) == 0);
  CHECK(!checked_narrow<std::uint8_t>(256u));
  CHECK(*checked_narrow<std::uint64_t>(std::uint8_t{255}) == 255);
  return true;
}
bool checked_count_atomic_update() {
  auto counter = CheckedCount<std::uint64_t>::create(3, 10);
  CHECK(counter);
  CHECK(counter->try_add(4) && counter->value() == 7);
  CHECK(!counter->try_add(4) && counter->value() == 7);
  CHECK(!counter->try_sub(8) && counter->value() == 7);
  CHECK(counter->try_sub(7) && counter->value() == 0);
  CHECK(!CheckedCount<unsigned>::create(5, 4));
  auto large = CheckedCount<std::uint64_t>::create(
      std::numeric_limits<std::uint64_t>::max());
  CHECK(!large->try_add(1) &&
        large->value() == std::numeric_limits<std::uint64_t>::max());
  return true;
}
bool invariant_fail_fast() {
  invariant(true);
  return true;
}
bool name_ascii_bounds() {
  CHECK(Name::parse("a"));
  CHECK(Name::parse(std::string(96, 'x')));
  for (auto text : {"", " ", "a b", "\x01", "\x7f", "中文"})
    CHECK(!Name::parse(text));
  CHECK(!Name::parse(std::string(97, 'x')));
  CHECK(!Name::parse(std::string_view("a\0b", 3)));
  return true;
}
bool name_case_and_ownership() {
  auto upper = Name::parse("Read");
  auto lower = Name::parse("read");
  CHECK(upper && lower && *upper != *lower && *upper == *Name::parse("Read"));
  auto own = []() {
    std::string text = "A_:-.!~";
    return Name::parse(text);
  }();
  CHECK(own && own->view() == "A_:-.!~");
  return true;
}
template <class A, class B>
concept Comparable = requires(A a, B b) { a == b; };
bool tagged_identity_types() {
  static_assert(!std::is_convertible_v<TaskId, ObjectId>);
  static_assert(!std::is_assignable_v<TaskId &, ObjectId>);
  static_assert(!Comparable<TaskId, ObjectId>);
  static_assert(!std::is_convertible_v<RuntimeEpoch, TaskId>);
  TaskId zero{}, one{{1}};
  CHECK(zero == TaskId{} && zero < one);
  return true;
}
bool identity_canonical_codec() {
  for (auto text :
       {"00000000000000000000000000000000", "ffffffffffffffffffffffffffffffff",
        "00112233445566778899aabbccddeeff"}) {
    auto id = parse_id<TaskTag>(text);
    CHECK(id);
    auto encoded = encode_id(*id);
    CHECK(encoded.size() == 32 &&
          std::string_view(encoded.data(), encoded.size()) == text);
  }
  for (auto text :
       {"00112233445566778899AABBCCDDEEFF",
        "00112233-4455-6677-8899-aabbccddeeff",
        " 00112233445566778899aabbccddeeff",
        "00112233445566778899aabbccddeeff ", "00112233445566778899aabbccddeefg",
        "00112233445566778899aabbccddeef", "00112233445566778899aabbccddeefff"})
    CHECK(!parse_id<TaskTag>(text));
  return true;
}
bool bound_handle_generation() {
  RegistryId registry{{1}}, other{{2}};
  RegistryGeneration generation{{3}}, old{{4}};
  BoundHandle<TaskTag> good{registry, generation, 2};
  CHECK(*resolve_slot(good, registry, generation, 3) == 2);
  auto stale = good;
  stale.generation = old;
  stale.slot = 999;
  CHECK(resolve_slot(stale, registry, generation, 0).error().code() ==
        make_error(FoundationErrc::stale_generation).code());
  auto foreign = good;
  foreign.registry = other;
  foreign.slot = 999;
  CHECK(resolve_slot(foreign, registry, generation, 0).error().code() ==
        make_error(FoundationErrc::wrong_registry).code());
  CHECK(!resolve_slot(BoundHandle<TaskTag>{}, registry, generation, 3));
  CHECK(!resolve_slot(good, registry, generation, 2));
  int indexed = 0;
  auto lookup = [&](const auto &handle) {
    auto slot = resolve_slot(handle, registry, generation, 3);
    if (slot) {
      ++indexed;
    }
    return slot;
  };
  CHECK(!lookup(stale) && !lookup(foreign) && indexed == 0);
  CHECK(lookup(good) && indexed == 1);
  return true;
}
bool runtime_epoch_separation() {
  TaskId id{{9}};
  auto before = encode_id(id);
  RuntimeEpoch epoch{std::numeric_limits<std::uint64_t>::max() - 1};
  CHECK(epoch.advance() &&
        epoch.value() == std::numeric_limits<std::uint64_t>::max());
  CHECK(!epoch.advance() &&
        epoch.value() == std::numeric_limits<std::uint64_t>::max());
  CHECK(before == encode_id(id));
  static_assert(!Comparable<RuntimeEpoch, TaskId>);
  return true;
}
struct Case {
  const char *name;
  bool (*run)();
};
#define CASE(family, name) {"T" #family ".foundation." #name, name}
const Case cases[] = {CASE(03, code_only_no_allocation),
                      CASE(03, value_operations_no_allocation),
                      CASE(03, layout_statistics),
                      CASE(04, result_move_only_value),
                      CASE(04, result_move_only_error),
                      CASE(04, result_void),
                      CASE(04, result_error_propagation),
                      CASE(04, result_bad_access),
                      CASE(04, exception_not_business_failure),
                      CASE(04, static_error_domain),
                      CASE(04, error_details_ownership),
                      CASE(04, error_details_budget),
                      CASE(04, display_utf8),
                      CASE(04, checked_arithmetic),
                      CASE(04, checked_narrowing),
                      CASE(04, checked_count_atomic_update),
                      CASE(04, invariant_fail_fast),
                      CASE(05, name_ascii_bounds),
                      CASE(05, name_case_and_ownership),
                      CASE(05, tagged_identity_types),
                      CASE(05, identity_canonical_codec),
                      CASE(05, bound_handle_generation),
                      CASE(05, runtime_epoch_separation)};
int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
    for (auto &item : cases)
      std::puts(item.name);
    return 0;
  }
  if (argc == 2 && (std::strcmp(argv[1], "--fail-fast") == 0 ||
                    std::strcmp(argv[1], "--fail-fast-noop") == 0)) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::puts("ock foundation invariant requested");
    std::fflush(stdout);
    invariant(std::strcmp(argv[1], "--fail-fast-noop") == 0);
    std::puts("ock foundation invariant unexpected-return");
    std::fflush(stdout);
    return 0;
  }
  if (argc != 2) {
    std::fputs("one registered case required\n", stderr);
    return 2;
  }
  for (auto &item : cases)
    if (std::strcmp(item.name, argv[1]) == 0) {
      try {
        if (!item.run())
          return 1;
        std::printf("%s Passed\n", item.name);
        return 0;
      } catch (const std::exception &e) {
        allocation::enabled = false;
        allocation::fail = false;
        std::fprintf(stderr, "unexpected exception: %s\n", e.what());
        return 1;
      }
    }
  std::fputs("unknown case\n", stderr);
  return 2;
}
