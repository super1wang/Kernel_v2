#include <ock/state/edit.hpp>
#include <algorithm>
#include <limits>

namespace ock::state {
foundation::Result<ObjectEdit> ObjectEdit::begin(ObjectRoot base,EditOptions options) {
  if(!options.root_bytes||!options.delta_bytes||!options.changed_objects)
    return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
  if(base.owned_bytes()>options.root_bytes)
    return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
  auto valid=base.validate_references();
  if(!valid)return foundation::make_unexpected(valid.error());
  return ObjectEdit{std::move(base),options};
}
foundation::Result<void> ObjectEdit::create(ObjectRecord record) {
  if(candidate_.find(record.id()))return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
  auto next=candidate_.replace(record,options_.root_bytes);
  if(!next)return foundation::make_unexpected(next.error());
  auto id=record.id();return update(id,std::move(record),std::move(*next));
}
foundation::Result<void> ObjectEdit::replace(ObjectRecord record) {
  if(!candidate_.find(record.id()))return foundation::make_unexpected(error(StateErrc::InvalidCandidate));
  auto next=candidate_.replace(record,options_.root_bytes);
  if(!next)return foundation::make_unexpected(next.error());
  auto id=record.id();return update(id,std::move(record),std::move(*next));
}
foundation::Result<void> ObjectEdit::erase(foundation::ObjectId id) {
  auto next=candidate_.erase(id);
  if(!next)return foundation::make_unexpected(next.error());
  return update(id,std::nullopt,std::move(*next));
}
foundation::Result<void> ObjectEdit::update(foundation::ObjectId id,
    std::optional<ObjectRecord> after,ObjectRoot next) {
  auto found=std::find_if(changes_.begin(),changes_.end(),[&](const auto& c){return c.id==id;});
  auto before=found==changes_.end()?candidate_.find(id):found->before;
  // 计量差量拥有的前后正文；算术先界检，失败不修改候选或 WriteSet。
  auto measure=[](const ObjectChange& c)->foundation::Result<std::size_t> {
    std::size_t bytes=sizeof(ObjectChange);
    for(const auto* record:{&c.before,&c.after})if(*record) {
      auto n=(*record)->owned_bytes();
      if(n>std::numeric_limits<std::size_t>::max()-bytes)
        return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
      bytes+=n;
    }
    return bytes;
  };
  ObjectChange change{id,std::move(before),std::move(after)};
  const bool cancelled=!change.before&&!change.after;
  auto old_bytes=found==changes_.end()?foundation::Result<std::size_t>{0}:measure(*found);
  auto new_bytes=cancelled?foundation::Result<std::size_t>{0}:measure(change);
  if(!old_bytes||!new_bytes)return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
  auto remaining=delta_bytes_-*old_bytes;
  if(*new_bytes>options_.delta_bytes||remaining>options_.delta_bytes-*new_bytes)
    return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
  if(!cancelled&&found==changes_.end()&&changes_.size()==options_.changed_objects)
    return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
  try {
    if(cancelled) {if(found!=changes_.end())changes_.erase(found);}
    else if(found==changes_.end())changes_.push_back(std::move(change));
    else *found=std::move(change);
    candidate_=std::move(next);delta_bytes_=remaining+*new_bytes;
    return {};
  } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
}
foundation::Result<FrozenRoot<ObjectRoot>> ObjectEdit::freeze() const {
  return FrozenRoot<ObjectRoot>::freeze(candidate_,options_.root_bytes);
}
}
