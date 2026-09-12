#pragma once
#include <ock/state/root.hpp>

namespace ock::state {
// 类型化正文有拥有者与稳定类型合同；不转换为通用 JSON 节点。
class ObjectValue final {
  struct Body {
    virtual ~Body()=default;
    virtual const contracts::TypeIdentity& type() const noexcept=0;
    virtual std::size_t bytes() const noexcept=0;
  };
  template<RootValue T> struct TypedBody final:Body {
    FrozenRoot<T> root;
    contracts::TypeIdentity identity;
    TypedBody(FrozenRoot<T> value,contracts::TypeIdentity type):root(std::move(value)),identity(std::move(type)) {}
    const contracts::TypeIdentity& type() const noexcept override {return identity;}
    std::size_t bytes() const noexcept override {return root.owned_bytes()+sizeof(TypedBody);}
  };
public:
  template<RootValue T> requires contracts::ContractValue<T>
  static foundation::Result<ObjectValue> freeze(const T& input,std::size_t maximum) {
    auto frozen=FrozenRoot<T>::freeze(input,maximum);
    if(!frozen)return foundation::make_unexpected(frozen.error());
    try {
      auto valid=contracts::TypeContract<T>::validate(frozen->value());
      if(!valid)return foundation::make_unexpected(valid.error());
      if(maximum<sizeof(TypedBody<T>)||frozen->owned_bytes()>maximum-sizeof(TypedBody<T>))
        return foundation::make_unexpected(error(StateErrc::BudgetExceeded));
      return ObjectValue{std::make_shared<const TypedBody<T>>(std::move(*frozen),contracts::TypeContract<T>::identity())};
    } catch(const std::bad_alloc&) {return foundation::make_unexpected(error(StateErrc::BudgetExceeded));}
    catch(...) {return foundation::make_unexpected(error(StateErrc::InvalidRoot));}
  }
  template<RootValue T> const T* get() const noexcept {
    auto typed=dynamic_cast<const TypedBody<T>*>(body_.get());return typed?&typed->root.value():nullptr;
  }
  const contracts::TypeIdentity& type() const noexcept {return body_->type();}
  std::size_t owned_bytes() const noexcept {return body_->bytes();}
private:
  explicit ObjectValue(std::shared_ptr<const Body> body):body_(std::move(body)) {}
  std::shared_ptr<const Body> body_;
};
class ObjectRecord final {
public:
  static foundation::Result<ObjectRecord> create(foundation::ObjectId,ObjectValue,
      std::span<const foundation::ObjectId> references,std::size_t maximum_references);
  foundation::ObjectId id() const noexcept {return id_;}
  const ObjectValue& value() const noexcept {return value_;}
  std::span<const foundation::ObjectId> references() const noexcept {return *references_;}
  std::size_t owned_bytes() const noexcept {return value_.owned_bytes()+sizeof(ObjectRecord)+references_->size()*sizeof(foundation::ObjectId);}
private:
  ObjectRecord(foundation::ObjectId id,ObjectValue value,std::shared_ptr<const std::vector<foundation::ObjectId>> references)
      :id_(id),value_(std::move(value)),references_(std::move(references)) {}
  foundation::ObjectId id_;
  ObjectValue value_;
  std::shared_ptr<const std::vector<foundation::ObjectId>> references_;
};
// 私有 persistent map 同时持有正文与反向引用；每次更新返回新根。
class ObjectRoot final {
public:
  ObjectRoot();
  std::size_t size() const noexcept;
  std::size_t owned_bytes() const noexcept;
  std::optional<ObjectRecord> find(foundation::ObjectId) const;
  bool referenced(foundation::ObjectId) const;
  foundation::Result<ObjectRoot> replace(ObjectRecord,std::size_t maximum_bytes) const;
  foundation::Result<ObjectRoot> erase(foundation::ObjectId) const;
  foundation::Result<void> validate_references() const;
private:
  struct Impl;
  explicit ObjectRoot(std::shared_ptr<const Impl> value):impl_(std::move(value)) {}
  std::shared_ptr<const Impl> impl_;
};
template<> struct RootContract<ObjectRoot> {
  static foundation::Result<ObjectRoot> freeze(const ObjectRoot& value) {
    auto valid=value.validate_references();if(!valid)return foundation::make_unexpected(valid.error());
    return value;
  }
  static foundation::Result<std::size_t> bytes(const ObjectRoot& value) {return value.owned_bytes();}
};
}
