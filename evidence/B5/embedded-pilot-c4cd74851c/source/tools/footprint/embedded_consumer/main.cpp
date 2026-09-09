#include "../consumer/channel.hpp"
#include <cstdio>
#ifdef OCK_EMBEDDED_CONSUMER
#include "../../../examples/embedded_service/fixture.hpp"
#endif
struct Hooks {
  footprint::Channel& channel;
  void stage(unsigned phase,unsigned owners,unsigned sentinels){channel.stage(phase,owners,sentinels);}
  template<class F> void measure(const char*,unsigned,bool,F&& work){work();}
};
int main(int argc,char** argv) {
  try {
    footprint::Channel channel(argc,argv);channel.stage(0,0,0);Hooks hooks{channel};
#ifdef OCK_EMBEDDED_CONSUMER
    embedded::run(hooks);
    std::puts(R"({"kind":"embedded","workers":2,"warmup":4,"invoke":40,"submit":40,"resource_child_cancel":true,"default_memory_log":true,"released":true})");
#else
    hooks.stage(1,0,0);hooks.stage(2,0,0);
    auto call=[](int value){volatile int input=value;footprint::require(input+1==value+1);};
    for(int i=0;i<4;++i)call(i);
    hooks.stage(3,0,0);hooks.stage(4,0,0);
    for(int i=0;i<40;++i){call(i);call(i);}
    hooks.stage(5,0,0);hooks.stage(6,0,0);hooks.stage(7,0,0);hooks.stage(8,0,0);
    hooks.stage(9,0,0);hooks.stage(10,0,0);
    std::puts(R"({"kind":"baseline","workers":0,"warmup":4,"local_calls":80,"released":true})");
#endif
    return 0;
  } catch(const std::exception& failure){std::fprintf(stderr,"%s\n",failure.what());return 1;}
}
