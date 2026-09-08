#include "backend.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <jsoncons/json.hpp>
#include <jsoncons/json_decoder.hpp>
#include <jsoncons/json_parser.hpp>
#include <scoped_allocator>
#include <set>

namespace ock::data {
namespace detail {
using Text = std::basic_string<char, std::char_traits<char>, Alloc<char>>;
struct Frame {
  bool object, pending = false;
  std::size_t items = 0;
  std::set<Text, std::less<>, Alloc<Text>> keys;
  explicit Frame(bool o) : object(o) {}
};
struct BuilderState final : jsoncons::json_visitor {
  std::unique_ptr<Owner> owner;
  std::unique_ptr<jsoncons::json_decoder<Json, Alloc<char>>> decoder;
  std::vector<Frame, Alloc<Frame>> frames;
  std::optional<DataErrc> failed;
  std::size_t nodes = 0, text = 0;
  bool root_seen = false;
  explicit BuilderState(Budget b)
      : owner(std::make_unique<Owner>(b)),
        frames(Alloc<Frame>(&owner->account)) {
    Scope scope(owner->account);
    owner->account.charge(sizeof(BuilderState) + sizeof(*decoder));
    decoder = std::make_unique<jsoncons::json_decoder<Json, Alloc<char>>>(
        JsonAlloc(Alloc<char>(&owner->account)), Alloc<char>(&owner->account));
  }
  void prepare() {
    if (nodes > (SIZE_MAX - 64) / 8 ||
        frames.size() > (SIZE_MAX - 64 - 8 * nodes) / 8)
      throw Failure{DataErrc::BudgetExceeded};
    // 上界覆盖 decoder item 栈扩容、对象收口的 key move 与嵌套 Frame 移动。
    owner->account.reserve_proxies(8 * nodes + 8 * frames.size() + 64);
  }
  void value() {
    prepare();
    if (frames.empty()) {
      if (root_seen)
        throw Failure{DataErrc::InvalidState};
      root_seen = true;
    } else {
      auto &f = frames.back();
      if (f.object && !f.pending)
        throw Failure{DataErrc::InvalidState};
      f.pending = false;
      if (f.items >= owner->account.limits.container_items)
        throw Failure{DataErrc::BudgetExceeded};
      ++f.items;
    }
    if (nodes >= owner->account.limits.nodes)
      throw Failure{DataErrc::BudgetExceeded};
    ++nodes;
  }
  void string_check(std::string_view s) {
    const auto &b = owner->account.limits;
    if (s.size() > b.token_bytes || s.size() > b.text_bytes - text)
      throw Failure{DataErrc::BudgetExceeded};
    if (!foundation::detail::valid_utf8(s))
      throw Failure{DataErrc::InvalidJson};
    text += s.size();
  }
  void begin(bool object) {
    value();
    if (frames.size() >= owner->account.limits.depth)
      throw Failure{DataErrc::BudgetExceeded};
    frames.emplace_back(object);
  }
  void end(bool object) {
    prepare();
    if (frames.empty() || frames.back().object != object ||
        frames.back().pending)
      throw Failure{DataErrc::InvalidState};
    frames.pop_back();
  }
  void visit_flush() override { decoder->flush(); }
  bool visit_begin_object(jsoncons::semantic_tag t,
                          const jsoncons::ser_context &c,
                          std::error_code &e) override {
    begin(true);
    return decoder->begin_object(t, c, e);
  }
  bool visit_end_object(const jsoncons::ser_context &c,
                        std::error_code &e) override {
    end(true);
    return decoder->end_object(c, e);
  }
  bool visit_begin_array(jsoncons::semantic_tag t,
                         const jsoncons::ser_context &c,
                         std::error_code &e) override {
    begin(false);
    return decoder->begin_array(t, c, e);
  }
  bool visit_end_array(const jsoncons::ser_context &c,
                       std::error_code &e) override {
    end(false);
    return decoder->end_array(c, e);
  }
  bool visit_key(const string_view_type &s, const jsoncons::ser_context &c,
                 std::error_code &e) override {
    prepare();
    if (frames.empty() || !frames.back().object || frames.back().pending)
      throw Failure{DataErrc::InvalidState};
    string_check(s);
    auto &f = frames.back();
    if (f.items >= owner->account.limits.container_items)
      throw Failure{DataErrc::BudgetExceeded};
    if (!f.keys.emplace(s.data(), s.size()).second)
      throw Failure{DataErrc::DuplicateKey};
    f.pending = true;
    return decoder->key(s, c, e);
  }
  bool visit_string(const string_view_type &s, jsoncons::semantic_tag t,
                    const jsoncons::ser_context &c,
                    std::error_code &e) override {
    if (t != jsoncons::semantic_tag::none)
      throw Failure{DataErrc::OutOfRange};
    string_check(s);
    value();
    return decoder->string_value(s, t, c, e);
  }
  bool visit_null(jsoncons::semantic_tag t, const jsoncons::ser_context &c,
                  std::error_code &e) override {
    value();
    return decoder->null_value(t, c, e);
  }
  bool visit_bool(bool v, jsoncons::semantic_tag t,
                  const jsoncons::ser_context &c, std::error_code &e) override {
    value();
    return decoder->bool_value(v, t, c, e);
  }
  bool visit_int64(std::int64_t v, jsoncons::semantic_tag t,
                   const jsoncons::ser_context &c,
                   std::error_code &e) override {
    value();
    return decoder->int64_value(v, t, c, e);
  }
  bool visit_uint64(std::uint64_t v, jsoncons::semantic_tag t,
                    const jsoncons::ser_context &c,
                    std::error_code &e) override {
    value();
    return decoder->uint64_value(v, t, c, e);
  }
  bool visit_double(double v, jsoncons::semantic_tag t,
                    const jsoncons::ser_context &c,
                    std::error_code &e) override {
    if (!std::isfinite(v))
      throw Failure{DataErrc::InvalidJson};
    value();
    return decoder->double_value(v, t, c, e);
  }
  bool visit_byte_string(const jsoncons::byte_string_view &,
                         jsoncons::semantic_tag, const jsoncons::ser_context &,
                         std::error_code &) override {
    throw Failure{DataErrc::InvalidJson};
  }
};
template <class F> Result<void> call(BuilderState *s, F f) {
  if (!s || !s->owner)
    return foundation::make_unexpected(error(DataErrc::InvalidState));
  if (s->failed)
    return foundation::make_unexpected(error(*s->failed));
  try {
    Scope scope(s->owner->account);
    f(*s);
    return {};
  } catch (const Failure &e) {
    s->failed = e.code;
    return foundation::make_unexpected(error(e.code));
  } catch (const jsoncons::ser_error &) {
    s->failed = DataErrc::InvalidJson;
    return foundation::make_unexpected(error(*s->failed));
  }
}
// 仅词法长度扫描，不生成第二棵树；在 parser 扩展 token 缓冲前拒绝。
void tokens(std::string_view text, std::size_t max) {
  bool quoted = false, escaped = false;
  std::size_t size = 0;
  for (char c : text) {
    if (quoted) {
      if (c == '"' && !escaped) {
        quoted = false;
        size = 0;
        continue;
      }
      if (++size > max)
        throw Failure{DataErrc::BudgetExceeded};
      if (escaped)
        escaped = false;
      else
        escaped = c == '\\';
    } else if (c == '"') {
      quoted = true;
      size = 0;
    } else if (c == '{' || c == '}' || c == '[' || c == ']' || c == ',' ||
               c == ':' || c == ' ' || c == '\r' || c == '\n' || c == '\t')
      size = 0;
    else if (++size > max)
      throw Failure{DataErrc::BudgetExceeded};
  }
}
} // namespace detail
Payload::Payload() noexcept = default;
Payload::~Payload() = default;
Payload::Payload(Payload &&) noexcept = default;
Payload &Payload::operator=(Payload &&) noexcept = default;
Payload::Payload(std::unique_ptr<detail::Owner> o) noexcept
    : owner_(std::move(o)) {}
PayloadBuilder::PayloadBuilder(Budget b) {
  try {
    state_ = std::make_unique<detail::BuilderState>(b);
  } catch (const detail::Failure &e) {
    initial_error_ = e.code;
  }
}
PayloadBuilder::~PayloadBuilder() = default;
PayloadBuilder::PayloadBuilder(PayloadBuilder &&) noexcept = default;
PayloadBuilder &PayloadBuilder::operator=(PayloadBuilder &&) noexcept = default;
#define OCK_DATA_EVENT(name, expr)                                             \
  Result<void> PayloadBuilder::name {                                          \
    if (initial_error_)                                                        \
      return foundation::make_unexpected(error(*initial_error_));              \
    return detail::call(state_.get(), [&](auto &s) { s.expr; });               \
  }
OCK_DATA_EVENT(begin_object(), begin_object())
OCK_DATA_EVENT(end_object(), end_object()) OCK_DATA_EVENT(begin_array(),
                                                          begin_array())
    OCK_DATA_EVENT(end_array(),
                   end_array()) OCK_DATA_EVENT(key(std::string_view v), key(v))
        OCK_DATA_EVENT(string(std::string_view v), string_value(v))
            OCK_DATA_EVENT(null(), null_value())
                OCK_DATA_EVENT(boolean(bool v), bool_value(v))
                    OCK_DATA_EVENT(int64(std::int64_t v), int64_value(v))
                        OCK_DATA_EVENT(uint64(std::uint64_t v), uint64_value(v))
                            OCK_DATA_EVENT(number(double v), double_value(v))
#undef OCK_DATA_EVENT
                                Result<Payload> PayloadBuilder::freeze() {
  if (initial_error_)
    return foundation::make_unexpected(error(*initial_error_));
  if (!state_ || !state_->owner)
    return foundation::make_unexpected(error(DataErrc::InvalidState));
  if (!state_->failed && (!state_->root_seen || !state_->frames.empty()))
    state_->failed = DataErrc::InvalidState;
  if (state_->failed)
    return foundation::make_unexpected(error(*state_->failed));
  auto &s = *state_;
  detail::Scope scope(s.owner->account);
  s.owner->root = s.decoder->get_result();
  s.decoder.reset();
  auto owner = std::move(s.owner);
  state_.reset();
  return Payload(std::move(owner));
}
Result<Payload> Payload::parse(std::string_view text, Budget b) {
  try {
    if (text.size() > b.frame_bytes)
      throw detail::Failure{DataErrc::BudgetExceeded};
    detail::tokens(text, b.token_bytes);
    PayloadBuilder builder(b);
    if (builder.initial_error_)
      return foundation::make_unexpected(error(*builder.initial_error_));
    auto &s = *builder.state_;
    detail::Scope scope(s.owner->account);
    jsoncons::json_options options;
    options.max_nesting_depth(
        static_cast<int>((std::min)(b.depth, std::size_t(INT_MAX))));
    options.err_handler([](auto, const auto &) { return false; });
    jsoncons::basic_json_parser<char, detail::Alloc<char>> parser(
        options, detail::Alloc<char>(&s.owner->account));
    parser.update(text.data(), text.size());
    parser.finish_parse(s);
    parser.check_done();
    return builder.freeze();
  } catch (const detail::Failure &e) {
    return foundation::make_unexpected(error(e.code));
  } catch (const jsoncons::ser_error &) {
    return foundation::make_unexpected(error(DataErrc::InvalidJson));
  }
}
ValueView Payload::view() const & noexcept {
  return ValueView(owner_ ? &owner_->root : nullptr);
}
ValueView SharedPayload::view() const & noexcept {
  return ValueView(owner_ ? &owner_->root : nullptr);
}
SharedPayload Payload::share() && { return SharedPayload(std::move(owner_)); }
std::size_t Payload::allocated_bytes() const noexcept {
  return owner_ ? owner_->account.allocated : 0;
}
Result<Payload> Payload::clone(Budget b) const {
  if (!owner_)
    return foundation::make_unexpected(error(DataErrc::InvalidState));
  try {
    PayloadBuilder copy(b);
    if (copy.initial_error_)
      return foundation::make_unexpected(error(*copy.initial_error_));
    auto result =
        detail::call(copy.state_.get(), [&](auto &s) { owner_->root.dump(s); });
    if (!result)
      return foundation::make_unexpected(result.error());
    return copy.freeze();
  } catch (const detail::Failure &e) {
    return foundation::make_unexpected(error(e.code));
  }
}
Result<std::string> Payload::encode(std::size_t max) const {
  if (!owner_)
    return foundation::make_unexpected(error(DataErrc::InvalidState));
  try {
    Budget b;
    b.allocation_bytes = max;
    detail::Account a{b};
    detail::Scope scope(a);
    detail::Text result;
    owner_->root.dump(result);
    if (result.size() > max)
      return foundation::make_unexpected(error(DataErrc::BudgetExceeded));
    return std::string(result.data(), result.size());
  } catch (const detail::Failure &e) {
    return foundation::make_unexpected(error(e.code));
  }
}
Kind ValueView::kind() const noexcept {
  if (!node_)
    return Kind::Missing;
  auto &j = *static_cast<const detail::Json *>(node_);
  if (j.is_null())
    return Kind::Null;
  if (j.is_bool())
    return Kind::Boolean;
  if (j.is_string())
    return Kind::String;
  if (j.is_array())
    return Kind::Array;
  if (j.is_object())
    return Kind::Object;
  if (j.type() == jsoncons::json_type::int64_value)
    return Kind::Int64;
  if (j.type() == jsoncons::json_type::uint64_value)
    return Kind::UInt64;
  return Kind::Number;
}
std::size_t ValueView::size() const noexcept {
  return node_ ? static_cast<const detail::Json *>(node_)->size() : 0;
}
ValueView ValueView::at(std::string_view key) const noexcept {
  if (kind() != Kind::Object)
    return ValueView();
  auto &j = *static_cast<const detail::Json *>(node_);
  auto i = j.find(key);
  return ValueView(i == j.object_range().end() ? nullptr : &i->value());
}
ValueView ValueView::at(std::size_t i) const noexcept {
  if (kind() != Kind::Array || i >= size())
    return ValueView();
  return ValueView(&(*static_cast<const detail::Json *>(node_))[i]);
}
Result<std::string_view> ValueView::key_at(std::size_t i) const noexcept {
  if (kind() != Kind::Object || i >= size())
    return foundation::make_unexpected(error(DataErrc::OutOfRange));
  auto it = static_cast<const detail::Json *>(node_)->object_range().begin();
  std::advance(it, i);
  return std::string_view(it->key().data(), it->key().size());
}
Result<std::string_view> ValueView::string() const noexcept {
  if (kind() != Kind::String)
    return foundation::make_unexpected(error(DataErrc::WrongType));
  auto s = static_cast<const detail::Json *>(node_)->as_string_view();
  return std::string_view(s.data(), s.size());
}
Result<bool> ValueView::boolean() const noexcept {
  if (kind() != Kind::Boolean)
    return foundation::make_unexpected(error(DataErrc::WrongType));
  return static_cast<const detail::Json *>(node_)->as_bool();
}
Result<std::int64_t> ValueView::int64() const noexcept {
  auto k = kind();
  if (k != Kind::Int64 && k != Kind::UInt64)
    return foundation::make_unexpected(error(DataErrc::WrongType));
  auto &j = *static_cast<const detail::Json *>(node_);
  if (k == Kind::UInt64 && j.as<std::uint64_t>() > INT64_MAX)
    return foundation::make_unexpected(error(DataErrc::OutOfRange));
  return j.as<std::int64_t>();
}
Result<std::uint64_t> ValueView::uint64() const noexcept {
  auto k = kind();
  if (k != Kind::Int64 && k != Kind::UInt64)
    return foundation::make_unexpected(error(DataErrc::WrongType));
  auto &j = *static_cast<const detail::Json *>(node_);
  if (k == Kind::Int64 && j.as<std::int64_t>() < 0)
    return foundation::make_unexpected(error(DataErrc::OutOfRange));
  return j.as<std::uint64_t>();
}
Result<double> ValueView::number() const noexcept {
  if (!node_)
    return foundation::make_unexpected(error(DataErrc::WrongType));
  auto &j = *static_cast<const detail::Json *>(node_);
  if (kind() == Kind::Number)
    return j.as_double();
  if (kind() == Kind::UInt64 && j.as<std::uint64_t>() <= 9007199254740992ULL)
    return static_cast<double>(j.as<std::uint64_t>());
  if (kind() == Kind::Int64 && j.as<std::int64_t>() >= -9007199254740992LL &&
      j.as<std::int64_t>() <= 9007199254740992LL)
    return static_cast<double>(j.as<std::int64_t>());
  return foundation::make_unexpected(error(DataErrc::OutOfRange));
}
Result<Buffer> Buffer::copy(std::span<const std::byte> v, std::size_t max) {
  if (v.size() > max)
    return foundation::make_unexpected(error(DataErrc::BudgetExceeded));
  return Buffer(
      std::make_shared<const std::vector<std::byte>>(v.begin(), v.end()));
}
} // namespace ock::data
