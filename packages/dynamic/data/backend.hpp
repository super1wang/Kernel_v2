#pragma once
// Data/Dynamic 专用内部桥；不安装、不导出第三方类型。
#include <jsoncons/json.hpp>
#include <ock/data/payload.hpp>
namespace ock::data::detail {
struct Failure {
  DataErrc code;
};
struct Account {
  Budget limits;
  std::size_t allocated = 0;
  explicit Account(Budget b) : limits(b) {
    if (b.depth > Budget::max_supported_depth)
      throw Failure{DataErrc::BudgetExceeded};
    reserve_proxies(64);
  }
  Account(const Account &) = delete;
#if _ITERATOR_DEBUG_LEVEL != 0
  struct ProxyCell {
    Account *owner;
    ProxyCell *next;
    alignas(
        std::_Container_proxy) std::byte value[sizeof(std::_Container_proxy)];
  };
  struct ProxyBlock {
    ProxyBlock *next;
    ProxyCell cells[64];
  };
  ProxyBlock *blocks = nullptr;
  ProxyCell *free = nullptr;
  std::size_t available = 0;
  ~Account() {
    while (blocks) {
      auto *next = blocks->next;
      delete blocks;
      blocks = next;
    }
  }
  void reserve_proxies(std::size_t count) {
    while (available < count) {
      charge(sizeof(ProxyBlock));
      auto *block = new ProxyBlock{};
      block->next = blocks;
      blocks = block;
      for (auto &cell : block->cells) {
        cell.owner = this;
        cell.next = free;
        free = &cell;
        ++available;
      }
    }
  }
  std::_Container_proxy *proxy() {
    // MSVC 的 noexcept string move 仍申请调试代理；所有槽提前在可抛错边界分配。
    if (!free)
      std::terminate();
    auto *cell = free;
    free = cell->next;
    --available;
    return reinterpret_cast<std::_Container_proxy *>(cell->value);
  }
  static void release_proxy(std::_Container_proxy *value) noexcept {
    auto *cell = reinterpret_cast<ProxyCell *>(
        reinterpret_cast<std::byte *>(value) - offsetof(ProxyCell, value));
    cell->next = cell->owner->free;
    cell->owner->free = cell;
    ++cell->owner->available;
  }
#else
  void reserve_proxies(std::size_t) {}
#endif
  void charge(std::size_t n) {
    if (n > limits.allocation_bytes - allocated)
      throw Failure{DataErrc::BudgetExceeded};
    allocated += n;
  }
};
inline thread_local Account *active = nullptr;
struct Scope {
  Account *old;
  explicit Scope(Account &a) : old(active) { active = &a; }
  ~Scope() { active = old; }
};
template <class T> struct Alloc {
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;
  // 所有块均由 std::allocator 分配/释放，释放资源可互换。account 仅为收费归属，
  // 不是独立堆；DOM 只在同一 owner 内移动，跨 owner clone 通过 SAX 重建。
  using is_always_equal = std::true_type;
  Account *account = active;
  Alloc() = default;
  explicit Alloc(Account *a) : account(a) {}
  template <class U> Alloc(const Alloc<U> &other) : account(other.account) {}
  T *allocate(std::size_t n) {
    if (!account || n > SIZE_MAX / sizeof(T))
      throw Failure{DataErrc::BudgetExceeded};
#if _ITERATOR_DEBUG_LEVEL != 0
    if constexpr (std::is_same_v<T, std::_Container_proxy>) {
      if (n != 1)
        std::terminate();
      return account->proxy();
    }
#endif
    account->charge(n * sizeof(T));
    return std::allocator<T>{}.allocate(n);
  }
  void deallocate(T *p, std::size_t n) noexcept {
#if _ITERATOR_DEBUG_LEVEL != 0
    if constexpr (std::is_same_v<T, std::_Container_proxy>) {
      Account::release_proxy(p);
      return;
    }
#endif
    std::allocator<T>{}.deallocate(p, n);
  }
  template <class U> bool operator==(const Alloc<U> &) const noexcept {
    return true;
  }
};
using JsonAlloc = Alloc<char>;
// 锁定版默认析构会为 flatten 栈再次分配；预算已耗尽时不得在 noexcept
// 析构中申请。 通过公开 Policy
// 扩展先清空，按已限制的深度递归释放；不修改第三方源码。
template <class J> struct Array final : jsoncons::json_array<J, std::vector> {
  using Base = jsoncons::json_array<J, std::vector>;
  using Base::Base;
  Array(const Array &) = default;
  Array(Array &&) noexcept = default;
  Array &operator=(const Array &) = default;
  Array &operator=(Array &&) noexcept = default;
  ~Array() noexcept { this->clear(); }
};
template <class K, class J>
struct Object final : jsoncons::sorted_json_object<K, J, std::vector> {
  using Base = jsoncons::sorted_json_object<K, J, std::vector>;
  using Base::Base;
  Object(const Object &) = default;
  Object(Object &&) noexcept = default;
  Object &operator=(const Object &) = default;
  Object &operator=(Object &&) noexcept = default;
  ~Object() noexcept { this->clear(); }
};
// 编译器元数据查表使用 std::string；键存储仍由原预算 allocator 拥有。
template <class C, class Tr, class A> struct Key : std::basic_string<C, Tr, A> {
  using Base = std::basic_string<C, Tr, A>;
  using Base::Base;
  Key() = default;
  Key(const Key &) = default;
  Key(Key &&) noexcept = default;
  Key &operator=(const Key &) = default;
  Key &operator=(Key &&) noexcept = default;
  operator std::basic_string<C, Tr>() const {
    return {this->data(), this->size()};
  }
  friend std::basic_string<C, Tr> operator+(const C *prefix, const Key &key) {
    std::basic_string<C, Tr> text(prefix);
    text.append(key.data(), key.size());
    return text;
  }
};
struct Policy : jsoncons::sorted_policy {
  template <class C, class Tr, class A> using member_key = Key<C, Tr, A>;
  template <class J> using array = Array<J>;
  template <class K, class J> using object = Object<K, J>;
};
using Json = jsoncons::basic_json<char, Policy, JsonAlloc>;
struct Owner {
  Account account;
  Json root;
  explicit Owner(Budget b) : account{b} { account.charge(sizeof(Owner)); }
};
struct BackendAccess {
  static const Json *node(ValueView value) noexcept {
    return static_cast<const Json *>(value.node_);
  }
};
} // namespace ock::data::detail
