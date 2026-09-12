#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/edit.hpp>
#include <chrono>

struct CostItem{std::array<std::uint64_t,8> words;};
template<>struct ock::state::RootContract<CostItem>{
  static Result<CostItem> freeze(const CostItem& value){return value;}
  static Result<std::size_t> bytes(const CostItem&){return sizeof(CostItem);}
};
template<>struct ock::contracts::TypeContract<CostItem>{
  static TypeIdentity identity(){return{name("state.cost-item"),ver(),{}};}
  static Result<void> validate(const CostItem&){return{};}
};
static foundation::ObjectId object_id(std::uint64_t value){
  foundation::ObjectId result{};for(unsigned i=0;i<8;++i)result.bytes[15-i]=static_cast<std::uint8_t>(value>>(i*8));return result;
}
int main()try{
  using namespace ock::state;constexpr std::size_t count=4096;
  ObjectRoot root;for(std::size_t i=0;i<count;++i){
    auto value=ObjectValue::freeze(CostItem{{i,i,i,i,i,i,i,i}},1024);CHECK(value);
    auto record=ObjectRecord::create(object_id(i+1),*value,{},4);CHECK(record);
    auto next=root.replace(*record,16*1024*1024);CHECK(next);root=std::move(*next);
  }
  auto unchanged=root.find(object_id(1));CHECK(unchanged);const auto* shared=unchanged->value().get<CostItem>();
  auto start=std::chrono::steady_clock::now();std::vector<ObjectRoot> snapshots(100000,root);
  auto copied=std::chrono::steady_clock::now();auto edit=ObjectEdit::begin(root,{16*1024*1024,4096,4});CHECK(edit);
  auto value=ObjectValue::freeze(CostItem{{9,9,9,9,9,9,9,9}},1024);CHECK(value);
  auto changed=ObjectRecord::create(object_id(count),*value,{},4);CHECK(changed&&edit->replace(*changed));
  auto finished=std::chrono::steady_clock::now();
  CHECK(root.find(object_id(1))->value().get<CostItem>()==shared);
  CHECK(edit->candidate().find(object_id(1))->value().get<CostItem>()==shared);
  CHECK(edit->candidate().size()==count&&edit->changes().size()==1);
  auto copy_us=std::chrono::duration_cast<std::chrono::microseconds>(copied-start).count();
  auto edit_us=std::chrono::duration_cast<std::chrono::microseconds>(finished-copied).count();
  std::cout<<"{\"objects\":"<<count<<",\"snapshot_copies\":"<<snapshots.size()
    <<",\"snapshot_copy_us\":"<<copy_us<<",\"single_edit_us\":"<<edit_us
    <<",\"root_owned_bytes\":"<<root.owned_bytes()<<",\"shared_body\":true}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
