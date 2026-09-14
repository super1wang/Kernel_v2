#pragma once
#include <ock/contracts/context.hpp>
#include <functional>
#include <atomic>
#include <memory>
#include <string>
namespace ock::runtime::resources {
using foundation::Result;
enum class Errc : std::uint32_t {InvalidInput=1, UnknownKey, Overflow, WouldBlock, Full, Closed, PhaseViolation, UnsafeChildWait};
inline constexpr foundation::ErrorDomain domain{"ock.resources"};
inline foundation::Error error(Errc c) noexcept {return foundation::Error{foundation::ErrorCode::make<domain>(static_cast<std::uint32_t>(c))};}
enum class Mode {Shared,Exclusive};
enum class Phase {Compute,Commit};
struct Slot {std::string key;std::uint64_t capacity=1;bool commit_only=false;};
struct Alias {std::string key,target;};
struct Claim {std::string key;Mode mode=Mode::Shared;std::uint64_t units=1;};
struct Options {std::size_t max_waiters=4096,max_claims=64;};
struct Snapshot {std::size_t slots=0,waiters=0,index_entries=0;std::uint64_t wakeups=0,callback_errors=0;};
class ResourceManager {
  struct State;
  struct Holding {std::size_t slot;Mode mode;std::uint64_t units;};
public:
  class Lease final : public contracts::ResourceLease {
  public:
    ~Lease() override;
    Lease(const Lease&)=delete;
    Lease& operator=(const Lease&)=delete;
    void release() noexcept;
    Result<void> before_child_wait(std::span<const Claim>) const;
  private:
    friend class ResourceManager;
    Lease(std::shared_ptr<State> s,std::vector<Holding> h):state_(std::move(s)),held_(std::move(h)){}
    std::shared_ptr<State> state_;
    std::vector<Holding> held_; // owning handle 本身的并发访问须由调用者同步。
  };
  class Waiter final {
  public:
    ~Waiter();
    Waiter(const Waiter&)=delete;
    Waiter& operator=(const Waiter&)=delete;
    void cancel() noexcept;
    std::uint64_t generation() const noexcept {return generation_;}
  private:
    friend class ResourceManager;
    Waiter(std::shared_ptr<State> s,std::uint64_t g,std::shared_ptr<std::atomic<bool>> active):state_(std::move(s)),generation_(g),active_(std::move(active)){}
    std::shared_ptr<State> state_;
    std::uint64_t generation_;
    std::shared_ptr<std::atomic<bool>> active_;
  };
  struct Acquisition {std::unique_ptr<Lease> lease;std::unique_ptr<Waiter> waiter;};
  static Result<std::unique_ptr<ResourceManager>> create(std::vector<Slot>,std::vector<Alias> = {},Options = {});
  ~ResourceManager();
  // 可信 resolver 提供 claims。wake 只是重试提示；每次重试仍原子申请全部资源。
  Result<Acquisition> acquire(std::span<const Claim>,Phase,std::function<void(std::uint64_t)> wake = {});
  Snapshot snapshot() const;
  std::uint64_t used(std::string_view key) const;
  void close() noexcept;
  static constexpr bool resource_factory=false;
private:
  explicit ResourceManager(std::shared_ptr<State> s):state_(std::move(s)){}
  std::shared_ptr<State> state_;
};
}
