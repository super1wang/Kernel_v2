#include "tests/compile/contracts/test_support.hpp"
#include <ock/state/root.hpp>
struct Config {std::vector<int> values;};
template<> struct ock::state::RootContract<Config> {
  static foundation::Result<Config> freeze(const Config& input) {return Config{input.values};}
  static foundation::Result<std::size_t> bytes(const Config& input) {return sizeof(Config)+input.values.capacity()*sizeof(int);}
};
#include <ock/state/detail/published_state.hpp>
int main() try {
  Config input{{1,2,3}};
  auto root=ock::state::FrozenRoot<Config>::freeze(input,1024);CHECK(root);
  input.values[0]=91;CHECK(root->value().values[0]==1);
  CHECK(!ock::state::FrozenRoot<Config>::freeze(input,1));
  using Published=ock::state::detail::PublishedState<Config>;
  auto old=Published::create(domain(),7,3,1,*root);CHECK(old);
  auto pin=*old;CHECK(&pin->root().value()==&root->value());
  CHECK(pin->revision()==7&&pin->history_cursor()==3&&pin->lifecycle_generation()==1);
  CHECK(!Published::create(domain(),8,4,0,*root));
  auto changed=ock::state::FrozenRoot<Config>::freeze(input,1024);CHECK(changed);
  auto current=Published::create(domain(),8,4,1,*changed);CHECK(current);
  CHECK((*current)->root().value().values[0]==91&&pin->root().value().values[0]==1);
  root=std::move(changed);old=std::move(current);
  CHECK(pin->revision()==7&&pin->root().value().values[0]==1);
  return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
