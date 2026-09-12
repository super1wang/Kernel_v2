#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/object_root.hpp>
#include <ock/state/edit.hpp>
struct Item {std::vector<int> values;};
template<> struct ock::state::RootContract<Item> {
  static Result<Item> freeze(const Item& v) {return Item{v.values};}
  static Result<std::size_t> bytes(const Item& v) {return sizeof(v)+v.values.capacity()*sizeof(int);}
};
template<> struct ock::contracts::TypeContract<Item> {
  static TypeIdentity identity() {return {name("state.item"),ver(),{}};}
  static Result<void> validate(const Item& v) {return v.values.empty()?reject(ContractsErrc::InvalidFact):Result<void>{};}
};
int main() try {
  using namespace ock::state;
  Item input{{1,2,3}};auto value=ObjectValue::freeze(input,4096);CHECK(value);
  input.values[0]=9;CHECK(value->get<Item>()->values[0]==1);
  ObjectRoot empty;auto a=id<foundation::ObjectId>(1),b=id<foundation::ObjectId>(2);
  auto first=ObjectRecord::create(a,*value,{},4);CHECK(first);
  auto root=empty.replace(*first,65536);CHECK(root&&root->size()==1&&empty.size()==0);
  auto old=*root;
  std::array refs{a};auto second=ObjectRecord::create(b,*value,refs,4);CHECK(second);
  auto both=root->replace(*second,65536);CHECK(both&&both->referenced(a)&&both->validate_references());
  CHECK(!both->erase(a)&&old.size()==1);
  CHECK(old.find(a)->value().get<Item>()==both->find(a)->value().get<Item>()); // 未变正文实际共享。
  auto unlinked=ObjectRecord::create(b,*value,{},4);CHECK(unlinked);
  auto updated=both->replace(*unlinked,65536);CHECK(updated&&!updated->referenced(a));
  auto removed=updated->erase(a);CHECK(removed&&removed->size()==1&&both->size()==2);
  auto dangling=empty.replace(*second,65536);CHECK(dangling&&!dangling->validate_references());
  CHECK(!FrozenRoot<ObjectRoot>::freeze(*dangling,65536));
  CHECK(!empty.replace(*first,1));
  std::array duplicate{a,a};CHECK(!ObjectRecord::create(b,*value,duplicate,4));
  auto edit=ObjectEdit::begin(old,{65536,65536,4});CHECK(edit);
  CHECK(!edit->create(*first));CHECK(edit->create(*second));
  CHECK(edit->find(b)&&edit->changes().size()==1&&!edit->changes()[0].before);
  CHECK(!edit->erase(a));CHECK(edit->replace(*unlinked));CHECK(edit->erase(a));
  CHECK(edit->freeze()&&old.size()==1&&old.find(a));
  CHECK(edit->changes().size()==2&&edit->changes()[1].before&&!edit->changes()[1].after);
  CHECK(edit->erase(b));CHECK(edit->changes().size()==1&&edit->candidate().size()==0);
  auto small=ObjectEdit::begin(old,{65536,1,1});CHECK(small);
  CHECK(!small->erase(a)&&small->find(a)&&small->changes().empty()&&small->delta_bytes()==0);
  auto unresolved=ObjectEdit::begin(empty,{65536,65536,4});CHECK(unresolved);
  CHECK(unresolved->create(*second)&&!unresolved->freeze());
  CHECK(unresolved->create(*first)&&unresolved->freeze());
  return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
