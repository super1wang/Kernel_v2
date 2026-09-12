#pragma once
#include <ock/state/root.hpp>

namespace ock::state::detail {
// root、revision、History 和生命周期只通过这一份不可变 owner 发布。
// 本材料没有发布权限；接收它的域仍须验证真实 reservation/provider owner。
template<RootValue T> class PublishedState final {
public:
  static foundation::Result<std::shared_ptr<const PublishedState>> create(
      contracts::AtomicDomainRef domain,std::uint64_t revision,
      std::uint64_t history_cursor,std::uint64_t lifecycle_generation,FrozenRoot<T> root) {
    if(!contracts::valid_domain(domain)||!lifecycle_generation)
      return foundation::make_unexpected(error(StateErrc::InvalidRoot));
    try {
      return std::shared_ptr<const PublishedState>(new PublishedState(
          std::move(domain),revision,history_cursor,lifecycle_generation,std::move(root)));
    } catch(const std::bad_alloc&) {
      return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    }
  }
  const contracts::AtomicDomainRef& domain() const noexcept {return domain_;}
  std::uint64_t revision() const noexcept {return revision_;}
  std::uint64_t history_cursor() const noexcept {return history_cursor_;}
  std::uint64_t lifecycle_generation() const noexcept {return lifecycle_generation_;}
  const FrozenRoot<T>& root() const noexcept {return root_;}
private:
  PublishedState(contracts::AtomicDomainRef domain,std::uint64_t revision,
      std::uint64_t history,std::uint64_t generation,FrozenRoot<T> root)
      :domain_(std::move(domain)),revision_(revision),history_cursor_(history),
       lifecycle_generation_(generation),root_(std::move(root)) {}
  contracts::AtomicDomainRef domain_;
  std::uint64_t revision_,history_cursor_,lifecycle_generation_;
  FrozenRoot<T> root_;
};
}
