#include <ock/state/object_root.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <algorithm>
#include <limits>

namespace ock::state {
namespace {
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
  using References=immer::set<foundation::ObjectId,ObjectHash>;
  immer::map<foundation::ObjectId,ObjectRecord,ObjectHash> objects;
  immer::map<foundation::ObjectId,References,ObjectHash> incoming;
  std::size_t bytes=sizeof(Impl);
};
ObjectRoot::ObjectRoot():impl_(std::make_shared<const Impl>()) {}
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
