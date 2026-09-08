#pragma once
#include <ock/dynamic/binding/record.hpp>
#include <ock/contracts/identity.hpp>
namespace ock::binding {
// TypeContract<T> 的显式特化继承此类，Native 与 Dynamic 使用相同字段/规则。
template<class T,class Spec> struct TypeContract : Record<T,Spec> {
  using FieldSpec=Spec;
  static contracts::TypeIdentity identity(){return Spec::identity();}
  static constexpr contracts::AsyncOwnership async_ownership=contracts::AsyncOwnership::Owning;
};
}
