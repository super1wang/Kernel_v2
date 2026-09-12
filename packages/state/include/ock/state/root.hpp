#pragma once
#include <ock/contracts/identity.hpp>
#include <concepts>
#include <memory>

namespace ock::state {
enum class StateErrc : std::uint32_t {
  InvalidRoot=1, BudgetExceeded, Closed, StaleGeneration, RevisionConflict,
  Busy, InvalidCandidate, ConstraintFailed, HistoryUnavailable, Exhausted
};
inline constexpr foundation::ErrorDomain state_domain{"ock.state"};
inline foundation::Error error(StateErrc value) noexcept {
  return foundation::Error{foundation::ErrorCode::make<state_domain>(static_cast<std::uint32_t>(value))};
}

// 由受信 provider 显式实现：freeze 必须切断所有可变别名，bytes 计量拥有内存。
// 不提供浅复制默认值；shared_ptr<const T> 本身不是冻结证明。
template<class T> struct RootContract;
template<class T> concept RootValue=requires(const T& value) {
  {RootContract<T>::freeze(value)} -> std::same_as<foundation::Result<T>>;
  {RootContract<T>::bytes(value)} -> std::same_as<foundation::Result<std::size_t>>;
};
template<RootValue T> class FrozenRoot final {
public:
  static foundation::Result<FrozenRoot> freeze(const T& input,std::size_t maximum) {
    if(!maximum)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    try {
      auto frozen=RootContract<T>::freeze(input);
      if(!frozen)return foundation::make_unexpected(frozen.error());
      auto measured=RootContract<T>::bytes(*frozen);
      if(!measured)return foundation::make_unexpected(measured.error());
      if(*measured>maximum)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
      return FrozenRoot{std::make_shared<const T>(std::move(*frozen)),*measured};
    } catch(const std::bad_alloc&) {
      return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    } catch(...) {
      return foundation::make_unexpected(error(StateErrc::InvalidRoot));
    }
  }
  const T& value() const noexcept {return *value_;}
  std::size_t owned_bytes() const noexcept {return bytes_;}
private:
  FrozenRoot(std::shared_ptr<const T> value,std::size_t bytes):value_(std::move(value)),bytes_(bytes) {}
  std::shared_ptr<const T> value_;
  std::size_t bytes_;
};
}
