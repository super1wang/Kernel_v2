#include <ock/runtime/resources.hpp>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <set>
namespace ock::runtime::resources {
using foundation::make_unexpected;
struct ResourceManager::State {
  struct Cell {Slot config;std::uint64_t used=0;bool exclusive=false;std::set<std::uint64_t> waiters;};
  struct Waiting {
    std::uint64_t generation;
    std::vector<Holding> claims;
    std::function<void(std::uint64_t)> wake;
    std::shared_ptr<std::atomic<bool>> active=std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<Waiting> next;
  };
  mutable std::mutex mutex;
  std::vector<Cell> cells;
  std::map<std::string,std::size_t,std::less<>> names;
  std::map<std::uint64_t,std::shared_ptr<Waiting>> waiting;
  Options options;
  std::uint64_t next_generation=1,wakeups=0,callback_errors=0;
  std::size_t index_entries=0,registered_or_pending=0;
  bool closing=false;
  Result<std::vector<Holding>> normalize(std::span<const Claim> claims,Phase phase) const {
    if(claims.empty() || claims.size()>options.max_claims || (phase!=Phase::Compute && phase!=Phase::Commit))return make_unexpected(error(Errc::InvalidInput));
    std::vector<Holding> normalized;
    try {
      for(auto &claim:claims) {
        auto it=names.find(claim.key);if(it==names.end())return make_unexpected(error(Errc::UnknownKey));
        if(!claim.units || (claim.mode!=Mode::Shared && claim.mode!=Mode::Exclusive))return make_unexpected(error(Errc::InvalidInput));
        auto &cell=cells[it->second];
        if(cell.config.commit_only && phase!=Phase::Commit)return make_unexpected(error(Errc::PhaseViolation));
        Holding *found=nullptr;for(auto &h:normalized)if(h.slot==it->second){found=&h;break;}
        if(found) {
          if(claim.units>std::numeric_limits<std::uint64_t>::max()-found->units)return make_unexpected(error(Errc::Overflow));
          found->units+=claim.units;if(claim.mode==Mode::Exclusive)found->mode=Mode::Exclusive;
        }else normalized.push_back({it->second,claim.mode,claim.units});
      }
      for(auto &h:normalized)if(h.units>cells[h.slot].config.capacity)return make_unexpected(error(Errc::InvalidInput));
      return normalized;
    }catch(...){return make_unexpected(error(Errc::Full));}
  }
  void unindex(const Waiting &w) noexcept {
    for(auto &h:w.claims)index_entries-=cells[h.slot].waiters.erase(w.generation);
  }
  void cancel(std::uint64_t generation) noexcept {
    std::shared_ptr<Waiting> released;
    {std::lock_guard lock(mutex);auto it=waiting.find(generation);if(it==waiting.end())return;
      released=std::move(it->second);*released->active=false;unindex(*released);waiting.erase(it);--registered_or_pending;}
  }
  void release(std::vector<Holding> held) noexcept {
    std::shared_ptr<Waiting> head,tail;
    {
      std::lock_guard lock(mutex);
      for(auto &h:held){auto &c=cells[h.slot];foundation::invariant(c.used>=h.units);c.used-=h.units;if(h.mode==Mode::Exclusive)c.exclusive=false;}
      // 不分配：摘除所有交叉索引，单个 generation 最多进入通知链一次。
      for(auto &h:held)while(!cells[h.slot].waiters.empty()) {
        auto it=waiting.find(*cells[h.slot].waiters.begin());auto w=it->second;
        unindex(*w);waiting.erase(it);
        if(tail)tail->next=w;else head=w;tail=std::move(w);
      }
    }
    while(head) {
      auto current=std::move(head);head=std::move(current->next);
      bool deliver=false;
      {std::lock_guard lock(mutex);--registered_or_pending;deliver=current->active->exchange(false) && !closing;if(deliver)++wakeups;}
      if(deliver) {
        try{current->wake(current->generation);}catch(...){std::lock_guard lock(mutex);++callback_errors;}
      }
    }
  }
};
Result<std::unique_ptr<ResourceManager>> ResourceManager::create(std::vector<Slot> slots,std::vector<Alias> aliases,Options options) {
  if(slots.empty() || slots.size()>256 || aliases.size()>1024 || !options.max_waiters || options.max_waiters>4096 || !options.max_claims || options.max_claims>64)return make_unexpected(error(Errc::InvalidInput));
  try {
    auto s=std::make_shared<State>();s->options=options;
    for(auto &slot:slots) {
      if(slot.key.empty() || slot.key.size()>256 || !slot.capacity || !s->names.emplace(slot.key,s->cells.size()).second)return make_unexpected(error(Errc::InvalidInput));
      s->cells.push_back(State::Cell{std::move(slot)});
    }
    for(auto &alias:aliases) {
      auto it=s->names.find(alias.target);
      if(alias.key.empty() || alias.key.size()>256 || it==s->names.end() || !s->names.emplace(alias.key,it->second).second)return make_unexpected(error(Errc::InvalidInput));
    }
    return std::unique_ptr<ResourceManager>(new ResourceManager(std::move(s)));
  }catch(...){return make_unexpected(error(Errc::Full));}
}
ResourceManager::~ResourceManager(){close();}
Result<ResourceManager::Acquisition> ResourceManager::acquire(std::span<const Claim> claims,Phase phase,std::function<void(std::uint64_t)> wake) {
  auto s=state_;std::shared_ptr<State::Waiting> pending;
  std::unique_lock lock(s->mutex);
  if(s->closing)return make_unexpected(error(Errc::Closed));
  auto normalized=s->normalize(claims,phase);if(!normalized)return make_unexpected(normalized.error());
  bool available=true;
  for(auto &h:*normalized){auto &c=s->cells[h.slot];if(c.exclusive || (h.mode==Mode::Exclusive && c.used) || h.units>c.config.capacity-c.used)available=false;}
  try {
    if(available) {
      auto lease=std::unique_ptr<Lease>(new Lease(s,std::move(*normalized)));
      for(auto &h:lease->held_){auto &c=s->cells[h.slot];c.used+=h.units;if(h.mode==Mode::Exclusive)c.exclusive=true;}
      return Acquisition{std::move(lease),{}};
    }
    if(!wake)return make_unexpected(error(Errc::WouldBlock));
    if(s->registered_or_pending>=s->options.max_waiters || !s->next_generation)return make_unexpected(error(Errc::Full));
    pending=std::make_shared<State::Waiting>();pending->generation=s->next_generation++;pending->claims=std::move(*normalized);pending->wake=std::move(wake);
    auto waiter=std::unique_ptr<Waiter>(new Waiter(s,pending->generation,pending->active));
    // handle 在索引分配失败时不能持锁析构并再次 cancel。
    try {
      s->waiting.emplace(pending->generation,pending);
      for(auto &h:pending->claims){s->cells[h.slot].waiters.insert(pending->generation);++s->index_entries;}
    }catch(...) {s->unindex(*pending);s->waiting.erase(pending->generation);lock.unlock();return make_unexpected(error(Errc::Full));}
    ++s->registered_or_pending;return Acquisition{{},std::move(waiter)};
  }catch(...){lock.unlock();return make_unexpected(error(Errc::Full));}
}
ResourceManager::Lease::~Lease(){release();}
void ResourceManager::Lease::release() noexcept {auto held=std::move(held_);held_.clear();if(!held.empty())state_->release(std::move(held));}
Result<void> ResourceManager::Lease::before_child_wait(std::span<const Claim> claims) const {
  std::lock_guard lock(state_->mutex);auto needed=state_->normalize(claims,Phase::Commit);if(!needed)return make_unexpected(needed.error());
  for(auto &a:held_)for(auto &b:*needed)if(a.slot==b.slot)return make_unexpected(error(Errc::UnsafeChildWait));
  return {};
}
ResourceManager::Waiter::~Waiter(){cancel();}
void ResourceManager::Waiter::cancel() noexcept {if(generation_){*active_=false;state_->cancel(generation_);generation_=0;}}
Snapshot ResourceManager::snapshot()const {std::lock_guard lock(state_->mutex);return {state_->cells.size(),state_->waiting.size(),state_->index_entries,state_->wakeups,state_->callback_errors};}
std::uint64_t ResourceManager::used(std::string_view key)const {std::lock_guard lock(state_->mutex);auto it=state_->names.find(key);return it==state_->names.end()?0:state_->cells[it->second].used;}
void ResourceManager::close() noexcept {
  std::map<std::uint64_t,std::shared_ptr<State::Waiting>> discarded;
  {std::lock_guard lock(state_->mutex);state_->closing=true;
    for(auto &[id,w]:state_->waiting){*w->active=false;state_->unindex(*w);}state_->registered_or_pending-=state_->waiting.size();discarded.swap(state_->waiting);}
}
}
