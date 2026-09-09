#include <charconv>
#include <ock/control/cursor.hpp>
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
namespace ock::control {
namespace {
inline constexpr foundation::ErrorDomain cursor_domain{"ock.control.cursor"};
template <class T> Result<T> invalid(bool expired = false) {
  return foundation::make_unexpected(foundation::Error{
      foundation::ErrorCode::make<cursor_domain>(expired ? 2 : 1)});
}
bool id(std::string_view value) {
  return value.size() == 32 &&
         value.find_first_not_of("0123456789abcdef") == value.npos;
}
std::optional<std::uint64_t> number(data::ValueView v) {
  auto text = v.string();
  if (!text || text->empty() || text->size() > 20 ||
      (text->size() > 1 && text->front() == '0') ||
      text->find_first_not_of("0123456789") != text->npos)
    return {};
  std::uint64_t n;
  auto r = std::from_chars(text->data(), text->data() + text->size(), n);
  if (r.ec != std::errc{})
    return {};
  return n;
}
bool valid(const CursorContext &c) {
  return id(c.host) && id(c.caller) && id(c.owner) && c.view &&
         (c.connection ? id(*c.connection) && c.delegation : c.delegation == 0) &&
         c.store.has_value() == c.restore.has_value() &&
         (!c.store || (id(*c.store) && *c.restore > 0)) &&
         (c.phases == "all" || c.phases == "terminal" ||
          c.phases == "nonterminal");
}
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string encode(std::span<const std::byte> bytes) {
  std::string out;
  unsigned bits = 0, value = 0;
  for (auto b : bytes) {
    value = (value << 8) | std::to_integer<unsigned char>(b);
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out += alphabet[(value >> bits) & 63];
    }
  }
  if (bits)
    out += alphabet[(value << (6 - bits)) & 63];
  return out;
}
Result<std::vector<std::byte>> decode(std::string_view text) {
  if (text.empty() || text.size() % 4 == 1)
    return invalid<std::vector<std::byte>>();
  std::vector<std::byte> out;
  unsigned bits = 0, value = 0;
  for (char c : text) {
    auto n = alphabet.find(c);
    if (n == alphabet.npos)
      return invalid<std::vector<std::byte>>();
    value = (value << 6) | static_cast<unsigned>(n);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(std::byte((value >> bits) & 255));
    }
  }
  if (encode(out) != text)
    return invalid<std::vector<std::byte>>();
  return out;
}
Result<std::string> canonical(const CursorContext &c, const CursorPosition &p) {
  // 固定 ASCII 字段只需直接编码；不为认证过的输入再造第二份 DOM。
  if (!valid(c))
    return invalid<std::string>();
  auto quoted = [](std::string_view text) {
    return "\"" + std::string(text) + "\"";
  };
  auto count = [&](std::uint64_t value) {
    return quoted(std::to_string(value));
  };
  auto principal = [&](std::string_view value) {
    return "{\"principal_id\":" + quoted(value) + "}";
  };
  return "{\"alg\":\"HS256\",\"caller\":" + principal(c.caller) +
         ",\"expires\":" + count(p.expires) +
         ",\"filter\":{\"owner\":" + principal(c.owner) +
         ",\"phase_set\":" + quoted(c.phases) + "},\"host\":" + quoted(c.host) +
         ",\"issued\":" + count(p.issued) +
         ",\"position\":" + count(p.position) +
         ",\"protocol\":\"ock.execution.list/1\",\"restore\":" +
         (c.restore ? count(*c.restore) : "null") +
         ",\"sort\":\"listing_ordinal_desc\",\"store\":" +
         (c.store ? quoted(*c.store) : "null") +
         ",\"upper\":" + count(p.upper) + ",\"view\":" + count(c.view) + "}";
}
Result<std::array<std::byte, 32>> mac(std::span<const std::byte, 32> secret,
                                      std::span<const std::byte> bytes, const CursorContext &context) {
  std::string input = "ock.execution.list/1";
  input += '\0';
  input.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (context.connection) {
    input += '\0';input += "ock.cursor.authorization/1";input += '\0';
    input += *context.connection;input += ':';input += std::to_string(context.delegation);
  }
  BCRYPT_ALG_HANDLE algorithm{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
    return invalid<std::array<std::byte, 32>>();
  std::array<std::byte, 32> output;
  auto status = BCryptHash(
      algorithm,
      reinterpret_cast<PUCHAR>(const_cast<std::byte *>(secret.data())), 32,
      reinterpret_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()),
      reinterpret_cast<PUCHAR>(output.data()), 32);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status < 0)
    return invalid<std::array<std::byte, 32>>();
  return output;
}
} // namespace
struct CursorCodec::State {
  std::string host;
  std::shared_ptr<const CursorClock> clock;
  std::array<std::byte, 32> secret;
  std::uint64_t utc, monotonic;
  ~State() { SecureZeroMemory(secret.data(), secret.size()); }
  std::optional<std::uint64_t> now() const {
    auto time = clock->monotonic_seconds();
    if (time < monotonic || time - monotonic > UINT64_MAX - utc)
      return {};
    return utc + time - monotonic;
  }
};
Result<CursorCodec>
CursorCodec::with_secret(std::string host,
                         std::shared_ptr<const CursorClock> clock,
                         std::array<std::byte, 32> secret) {
  if (!id(host) || !clock)
    return invalid<CursorCodec>();
  auto state = std::make_shared<State>();
  state->host = std::move(host);
  state->clock = std::move(clock);
  state->secret = secret;
  SecureZeroMemory(secret.data(), secret.size());
  state->utc = state->clock->utc_seconds();
  state->monotonic = state->clock->monotonic_seconds();
  return CursorCodec(std::move(state));
}
Result<CursorCodec>
CursorCodec::create(std::string host,
                    std::shared_ptr<const CursorClock> clock) {
  std::array<std::byte, 32> secret;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(secret.data()), 32,
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    return invalid<CursorCodec>();
  auto result = with_secret(std::move(host), std::move(clock), secret);
  SecureZeroMemory(secret.data(), secret.size());
  return result;
}
Result<std::string> CursorCodec::issue(const CursorContext &context,
                                       std::uint64_t upper,
                                       std::uint64_t position) const {
  auto now = state_->now();
  if (!now || *now > UINT64_MAX - 120)
    return invalid<std::string>();
  return issue(context, CursorPosition{upper, position, *now, *now + 120});
}
Result<std::string> CursorCodec::issue(const CursorContext &context,
                                       const CursorPosition &p) const {
  auto now = state_->now();
  if (!now || !valid(context) || context.host != state_->host || !p.position ||
      p.position > p.upper || p.issued > *now || p.expires <= p.issued ||
      p.expires - p.issued > 120)
    return invalid<std::string>();
  if (*now >= p.expires)
    return invalid<std::string>(true);
  auto payload = canonical(context, p);
  if (!payload)
    return payload;
  auto bytes = std::as_bytes(std::span(*payload));
  auto signature = mac(state_->secret, bytes, context);
  if (!signature)
    return foundation::make_unexpected(signature.error());
  auto token = "v1." + encode(bytes) + "." + encode(*signature);
  if (token.size() > 2048)
    return invalid<std::string>();
  return token;
}
Result<CursorPosition> CursorCodec::read(std::string_view token,
                                         const CursorContext &context) const {
  if (!valid(context) || context.host != state_->host || token.size() > 2048 ||
      !token.starts_with("v1."))
    return invalid<CursorPosition>();
  auto dot = token.find('.', 3);
  if (dot == token.npos || token.find('.', dot + 1) != token.npos)
    return invalid<CursorPosition>();
  auto payload = decode(token.substr(3, dot - 3)),
       signature = decode(token.substr(dot + 1));
  if (!payload || !signature || signature->size() != 32)
    return invalid<CursorPosition>();
  auto expected = mac(state_->secret, *payload, context);
  if (!expected)
    return foundation::make_unexpected(expected.error());
  unsigned difference = 0;
  for (std::size_t i = 0; i < 32; ++i)
    difference |=
        std::to_integer<unsigned char>((*signature)[i] ^ (*expected)[i]);
  if (difference)
    return invalid<CursorPosition>();
  const std::string_view text(reinterpret_cast<const char *>(payload->data()),
                              payload->size());
  auto parsed = data::Payload::parse(text);
  if (!parsed)
    return invalid<CursorPosition>();
  auto v = parsed->view();
  auto upper = number(v.at("upper")), position = number(v.at("position")),
       issued = number(v.at("issued")), expires = number(v.at("expires"));
  if (!upper || !position || !issued || !expires)
    return invalid<CursorPosition>();
  CursorPosition p{*upper, *position, *issued, *expires};
  // 对当前可信 context
  // 重建固定字段：同时拒绝未知字段、世代/身份/过滤差异及非规范 JSON。
  auto normalized = canonical(context, p);
  if (!normalized || *normalized != text)
    return invalid<CursorPosition>();
  // 只校验位置/期限，不为了检查而重新签名和分配一个 token。
  auto now=state_->now();
  if(!now || !p.position || p.position>p.upper || p.issued>*now ||
     p.expires<=p.issued || p.expires-p.issued>120)
    return invalid<CursorPosition>();
  if(*now>=p.expires)return invalid<CursorPosition>(true);
  return p;
}
} // namespace ock::control
