#include "fixture.hpp"
struct Hooks {
  void stage(unsigned,unsigned,unsigned){}
  template<class F> void measure(const char*,unsigned,bool,F&& work){work();}
};
int main() {
  try {Hooks hooks;embedded::run(hooks);return 0;}
  catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}
}
