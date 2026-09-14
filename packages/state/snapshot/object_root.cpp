#include <ock/state/object_root.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <algorithm>
#include <limits>
#include <mutex>
#include <cstddef>
#include <stdexcept>

namespace ock::state {
namespace {
struct IndexLedger {
  explicit IndexLedger(std::size_t limit):value{limit,0,0,0,0,0,0} {}
  std::mutex mutex;IndexMemory value;
};
thread_local std::shared_ptr<IndexLedger> allocating_index;
struct IndexScope {
  std::shared_ptr<IndexLedger> previous;
  explicit IndexScope(std::shared_ptr<IndexLedger> ledger):previous(std::move(allocating_index)){allocating_index=std::move(ledger);}
  ~IndexScope(){allocating_index=std::move(previous);}
};
struct IndexHeap {
  struct alignas(std::max_align_t) Header {std::shared_ptr<IndexLedger> ledger;std::size_t bytes;};
  template<class... Tags> static void* allocate(std::size_t size,Tags...) {
    if(size>std::numeric_limits<std::size_t>::max()-sizeof(Header))throw std::bad_alloc{};
    const auto bytes=size+sizeof(Header);auto ledger=allocating_index;
    if(ledger) {
      std::lock_guard lock(ledger->mutex);auto& v=ledger->value;
      if(bytes>v.limit||v.retained_bytes>v.limit-bytes||v.allocated_bytes>std::numeric_limits<std::size_t>::max()-bytes||
          v.allocations==std::numeric_limits<std::size_t>::max()||v.live_nodes==std::numeric_limits<std::size_t>::max())throw std::bad_alloc{};
      v.retained_bytes+=bytes;v.allocated_bytes+=bytes;++v.allocations;++v.live_nodes; // Reserve against concurrent descendants before allocating.
    }
    void* raw=nullptr;
    try {raw=::operator new(bytes);}catch(...) {
      if(ledger){std::lock_guard lock(ledger->mutex);auto& v=ledger->value;v.retained_bytes-=bytes;v.allocated_bytes-=bytes;--v.allocations;--v.live_nodes;}throw;
    }
    auto header=new(raw) Header{ledger,bytes};
    if(ledger) {std::lock_guard lock(ledger->mutex);auto& v=ledger->value;
      v.peak_bytes=std::max(v.peak_bytes,v.retained_bytes);}
    return header+1;
  }
  static void deallocate(std::size_t,void* pointer) noexcept {
    auto header=static_cast<Header*>(pointer)-1;auto ledger=header->ledger;const auto bytes=header->bytes;
    if(ledger){std::lock_guard lock(ledger->mutex);auto& v=ledger->value;v.retained_bytes-=bytes;v.freed_bytes+=bytes;--v.live_nodes;}
    header->~Header();::operator delete(header);
  }
};
using IndexPolicy=immer::memory_policy<immer::heap_policy<IndexHeap>,immer::refcount_policy,immer::spinlock_policy>;
struct ObjectHash {
  std::size_t operator()(const foundation::ObjectId& id) const noexcept {
    std::size_t value=1469598103934665603ull;
    for(auto byte:id.bytes)value=(value^byte)*1099511628211ull;
    return value;
  }
};
}
foundation::Result<ObjectRecord> ObjectRecord::create(foundation::ObjectId id,ObjectValue value,
    std::span<const foundation::ObjectId> refs,std::size_t maximum) {
  if(id.empty()||refs.size()>maximum||value.owned_bytes()>std::numeric_limits<std::size_t>::max()-sizeof(ObjectRecord)||
      refs.size()>(std::numeric_limits<std::size_t>::max()-sizeof(ObjectRecord)-value.owned_bytes())/sizeof(foundation::ObjectId))
    return foundation::make_unexpected(error(StateErrc::InvalidRoot));
  try {
    std::vector<foundation::ObjectId> copy(refs.begin(),refs.end());
    std::sort(copy.begin(),copy.end(),[](const auto& a,const auto& b){return a.bytes<b.bytes;});
    if(std::any_of(copy.begin(),copy.end(),[](const auto& x){return x.empty();})||std::adjacent_find(copy.begin(),copy.end())!=copy.end())
      return foundation::make_unexpected(error(StateErrc::InvalidRoot));
    return ObjectRecord{id,std::move(value),std::make_shared<const std::vector<foundation::ObjectId>>(std::move(copy))};
  } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
}
struct ObjectRoot::Impl {
  using References=immer::set<foundation::ObjectId,ObjectHash,std::equal_to<foundation::ObjectId>,IndexPolicy>;
  immer::map<foundation::ObjectId,ObjectRecord,ObjectHash,std::equal_to<foundation::ObjectId>,IndexPolicy> objects;
  immer::map<foundation::ObjectId,References,ObjectHash,std::equal_to<foundation::ObjectId>,IndexPolicy> incoming;
  std::size_t bytes=sizeof(Impl);
  std::shared_ptr<IndexLedger> ledger;
  explicit Impl(std::shared_ptr<IndexLedger> value):ledger(std::move(value)) {
    static const References empty_references; (void)empty_references;
  }
};
ObjectRoot::ObjectRoot():ObjectRoot(64*1024*1024) {}
ObjectRoot::ObjectRoot(std::size_t index_bytes) {
  if(!index_bytes||index_bytes>64*1024*1024)throw std::invalid_argument("State index budget must be within 1..64 MiB");
  IndexScope initialize_singletons({});
  impl_=std::make_shared<const Impl>(std::make_shared<IndexLedger>(index_bytes));
}
IndexMemory ObjectRoot::index_memory() const noexcept {std::lock_guard lock(impl_->ledger->mutex);return impl_->ledger->value;}
std::size_t ObjectRoot::logical_bytes() const noexcept {return impl_->bytes;}
std::size_t ObjectRoot::size() const noexcept {return impl_->objects.size();}
std::size_t ObjectRoot::owned_bytes() const noexcept {return impl_->bytes;}
std::optional<ObjectRecord> ObjectRoot::find(foundation::ObjectId id) const {
  if(auto value=impl_->objects.find(id))return *value;
  return {};
}
bool ObjectRoot::referenced(foundation::ObjectId id) const {
  auto refs=impl_->incoming.find(id);return refs&&!refs->empty();
}
foundation::Result<ObjectRoot> ObjectRoot::replace(ObjectRecord record,std::size_t maximum) const {
  try {
    auto old=impl_->objects.find(record.id());
    const auto retained=impl_->bytes-(old?old->owned_bytes():0);
    if(retained>maximum||record.owned_bytes()>maximum-retained)
      return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
    IndexScope allocation(impl_->ledger);
    auto next=std::make_shared<Impl>(*impl_);
    if(old)for(auto target:old->references()) {
      auto users=next->incoming.find(target);foundation::invariant(users!=nullptr);
      auto remaining=users->erase(old->id());
      next->incoming=remaining.empty()?next->incoming.erase(target):next->incoming.set(target,std::move(remaining));
    }
    for(auto target:record.references()) {
      auto previous=next->incoming.find(target);
      auto users=previous?*previous:Impl::References{};
      next->incoming=next->incoming.set(target,users.insert(record.id()));
    }
    next->bytes=retained+record.owned_bytes();
    next->objects=next->objects.set(record.id(),std::move(record));
    return ObjectRoot{std::move(next)};
  } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
}
foundation::Result<ObjectRoot> ObjectRoot::erase(foundation::ObjectId id) const {
  auto old=impl_->objects.find(id);
  if(!old)return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
  if(referenced(id))return foundation::make_unexpected(error(StateErrc::ConstraintFailed));
  try {
    IndexScope allocation(impl_->ledger);
    auto next=std::make_shared<Impl>(*impl_);
    for(auto target:old->references()) {
      auto users=next->incoming.find(target);foundation::invariant(users!=nullptr);
      auto remaining=users->erase(id);
      next->incoming=remaining.empty()?next->incoming.erase(target):next->incoming.set(target,std::move(remaining));
    }
    next->objects=next->objects.erase(id);next->bytes-=old->owned_bytes();
    return ObjectRoot{std::move(next)};
  } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
}
foundation::Result<void> ObjectRoot::validate_references() const {
  for(const auto& pair:impl_->incoming)
    if(!impl_->objects.find(pair.first))return foundation::make_unexpected(error(StateErrc::ConstraintFailed));
  return {};
}
}
