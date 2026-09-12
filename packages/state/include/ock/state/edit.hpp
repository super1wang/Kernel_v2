#pragma once
#include <ock/state/object_root.hpp>

namespace ock::state {
struct EditOptions {
  std::size_t root_bytes;
  std::size_t delta_bytes;
  std::size_t changed_objects;
};
struct ObjectChange {
  foundation::ObjectId id;
  std::optional<ObjectRecord> before;
  std::optional<ObjectRecord> after;
};
// 只拥有候选及正逆差量；本类型没有发布权限。
class ObjectEdit final {
public:
  static foundation::Result<ObjectEdit> begin(ObjectRoot base,EditOptions options);
  std::optional<ObjectRecord> find(foundation::ObjectId id) const {return candidate_.find(id);}
  const ObjectRoot& candidate() const noexcept {return candidate_;}
  std::span<const ObjectChange> changes() const noexcept {return changes_;}
  std::size_t delta_bytes() const noexcept {return delta_bytes_;}
  foundation::Result<void> create(ObjectRecord);
  foundation::Result<void> replace(ObjectRecord);
  foundation::Result<void> erase(foundation::ObjectId);
  foundation::Result<FrozenRoot<ObjectRoot>> freeze() const;
private:
  ObjectEdit(ObjectRoot root,EditOptions options):candidate_(std::move(root)),options_(options) {}
  foundation::Result<void> update(foundation::ObjectId,std::optional<ObjectRecord>,ObjectRoot);
  ObjectRoot candidate_;
  EditOptions options_;
  std::vector<ObjectChange> changes_;
  std::size_t delta_bytes_=0;
};
}
