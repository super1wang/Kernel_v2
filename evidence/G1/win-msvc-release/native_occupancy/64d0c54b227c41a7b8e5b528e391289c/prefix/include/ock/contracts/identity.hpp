#pragma once
// D1.02：稳定身份与进程内类型见证，禁止把token持久化。
#include <functional>
#include <ock/foundation/foundation.hpp>
#include <optional>
#include <span>
#include <variant>
#include <vector>
namespace ock::contracts {
using foundation::Error;
using foundation::make_unexpected;
using foundation::Name;
using foundation::Result;
inline constexpr foundation::ErrorDomain contracts_domain{"ock.contracts"};
enum class ContractsErrc : std::uint32_t {
  InvalidVersion = 1,
  InvalidContract,
  TypeMismatch,
  ShapeMismatch,
  StaleBinding,
  InvalidAuthority,
  InvalidGrant,
  InvalidProof,
  InvalidFact,
  ContradictoryFact,
  InvalidPhase,
  BudgetExceeded,
  DuplicateCompletion,
  NotQuiescent,
  HostMismatch,
  StaleObservation,
  SubmitRequired,
  Rejected
};
inline Error error(ContractsErrc code) noexcept {
  return Error{foundation::ErrorCode::make<contracts_domain>(
      static_cast<std::uint32_t>(code))};
}
inline Result<void> reject(ContractsErrc code) noexcept {
  return make_unexpected(error(code));
}
class OperationVersion final {
public:
  static Result<OperationVersion> parse(std::string_view text,
                                        std::size_t max_bytes) {
    if (text.empty() || text.size() > max_bytes)
      return make_unexpected(error(ContractsErrc::InvalidVersion));
    std::size_t start = 0;
    unsigned parts = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
      if (i == text.size() || text[i] == '.') {
        if (i == start || (i - start > 1 && text[start] == '0'))
          return make_unexpected(error(ContractsErrc::InvalidVersion));
        ++parts;
        start = i + 1;
      } else if (text[i] < '0' || text[i] > '9')
        return make_unexpected(error(ContractsErrc::InvalidVersion));
    }
    if (parts != 3)
      return make_unexpected(error(ContractsErrc::InvalidVersion));
    return OperationVersion{std::string(text)};
  }
  std::string_view text() const noexcept { return text_; }
  bool operator==(const OperationVersion &) const noexcept = default;

private:
  explicit OperationVersion(std::string text) : text_(std::move(text)) {}
  std::string text_;
};
struct OperationKey {
  Name name;
  OperationVersion version;
  bool operator==(const OperationKey &) const = default;
};
struct ContractDigest {
  std::array<std::byte, 32> bytes{};
  bool operator==(const ContractDigest &) const = default;
};
struct TypeIdentity {
  Name name;
  OperationVersion version;
  ContractDigest digest;
  bool operator==(const TypeIdentity &) const = default;
};
class CppTypeToken final {
public:
  template <class T> static CppTypeToken of() noexcept {
    static unsigned char anchor;
    return CppTypeToken{&anchor};
  }
  bool operator==(const CppTypeToken &) const noexcept = default;

private:
  explicit CppTypeToken(const unsigned char *p) noexcept : anchor_(p) {}
  const unsigned char *anchor_;
};
enum class AsyncOwnership { Disallowed, Owning };
template <class T> struct TypeContract;
template <> struct TypeContract<void> {
  static TypeIdentity identity() {
    return {*Name::parse("ock.void"), *OperationVersion::parse("1.0.0", 5), {}};
  }
  static constexpr AsyncOwnership async_ownership = AsyncOwnership::Owning;
};
template <class T>
concept ContractValue = std::same_as<T, std::remove_cvref_t<T>> &&
                        std::destructible<T> && requires(const T &value) {
                          {
                            TypeContract<T>::identity()
                          } -> std::same_as<TypeIdentity>;
                          {
                            TypeContract<T>::validate(value)
                          } -> std::same_as<Result<void>>;
                        };
namespace detail {
template <class T>
inline constexpr bool borrowed = std::is_pointer_v<T> || std::is_reference_v<T>;
template <class C, class Tr>
inline constexpr bool borrowed<std::basic_string_view<C, Tr>> = true;
template <class T, std::size_t N>
inline constexpr bool borrowed<std::span<T, N>> = true;
template <class T>
inline constexpr bool borrowed<std::reference_wrapper<T>> = true;
template <class T> constexpr bool owning() {
  if constexpr (requires {
                  requires std::same_as<
                      std::remove_cv_t<decltype(TypeContract<T>::async_ownership)>,
                      AsyncOwnership>;
                  typename std::integral_constant<
                      AsyncOwnership, TypeContract<T>::async_ownership>;
                })
    return TypeContract<T>::async_ownership == AsyncOwnership::Owning;
  return false;
}
} // namespace detail
template <class T>
concept AsyncInput =
    ContractValue<T> && !detail::borrowed<T> && detail::owning<T>();
template <class T>
concept ContractResult = std::same_as<T, void> || ContractValue<T>;
#define OCK_CONTRACT_ID(Name_)                                                 \
  struct Name_##Tag {};                                                        \
  using Name_ = foundation::Tagged128<Name_##Tag>
OCK_CONTRACT_ID(CommitId);
OCK_CONTRACT_ID(EffectId);
OCK_CONTRACT_ID(TransitionId);
OCK_CONTRACT_ID(PrincipalId);
OCK_CONTRACT_ID(HostIncarnation);
OCK_CONTRACT_ID(FactId);
OCK_CONTRACT_ID(ReservationId);
#undef OCK_CONTRACT_ID
struct ExecutionRef {
  foundation::TaskId execution_id;
  bool operator==(const ExecutionRef &) const = default;
};
struct PrincipalRef {
  PrincipalId principal_id;
  bool operator==(const PrincipalRef &) const = default;
};
using AtomicProviderKey = Name;
struct AtomicDomainRef {
  AtomicProviderKey provider;
  foundation::ObjectId domain_id;
  foundation::RegistryGeneration generation;
  bool operator==(const AtomicDomainRef &) const = default;
};
inline bool valid_domain(const AtomicDomainRef &d) noexcept {
  return !d.domain_id.empty() && !d.generation.empty();
}
inline Result<void> validate_atomic_domain(const AtomicDomainRef &a,
                                           const AtomicDomainRef &b) {
  if (!valid_domain(a) || !valid_domain(b) || a != b)
    return reject(ContractsErrc::InvalidContract);
  return {};
}
// 抽象领域端口共同的非复制寿命约束，不携带任何服务或运行时状态。
class PortLifetime {
public:
  virtual ~PortLifetime() = default;
  PortLifetime(const PortLifetime &) = delete;
  PortLifetime &operator=(const PortLifetime &) = delete;

protected:
  PortLifetime() = default;
};
} // namespace ock::contracts
