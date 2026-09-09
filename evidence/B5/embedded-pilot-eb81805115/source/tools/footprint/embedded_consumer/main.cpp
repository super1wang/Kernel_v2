#include "../consumer/channel.hpp"
#include <cstdio>
#ifdef OCK_FOOTPRINT_ALLOCATION
#include "allocation.hpp"
#endif
#ifdef OCK_EMBEDDED_CONSUMER
#include "../../../examples/embedded_service/fixture.hpp"
#endif
struct Hooks {
  footprint::Channel& channel;
#ifdef OCK_FOOTPRINT_ALLOCATION
  embedded_allocation::Recorder recorder;
#endif
  std::uint64_t construction_begin=0,construction_ticks=0;
  void begin_construction(){construction_begin=footprint::ticks();}
  void ready(){construction_ticks=footprint::ticks()-construction_begin;}
  void stage(unsigned phase,unsigned owners,unsigned sentinels){channel.stage(phase,owners,sentinels);}
  template<class F> void measure(const char* label,unsigned index,bool zero,F&& work){
#ifdef OCK_FOOTPRINT_ALLOCATION
    recorder.measure(label,index,zero,std::forward<F>(work));
#else
    work();
#endif
  }
};
int main(int argc,char** argv) {
  try {
    footprint::Channel channel(argc,argv);channel.stage(0,0,0);Hooks hooks{channel};
#ifdef OCK_FOOTPRINT_ALLOCATION
    hooks.recorder.initialize();
#endif
    LARGE_INTEGER frequency{};footprint::require(QueryPerformanceFrequency(&frequency)!=0&&frequency.QuadPart>0);
#ifdef OCK_EMBEDDED_CONSUMER
    embedded::run(hooks);
#ifdef OCK_FOOTPRINT_ALLOCATION
    hooks.recorder.emit("embedded");
#endif
    std::printf(R"({"kind":"embedded","workers":2,"warmup":4,"invoke":40,"submit":40,"resource_child_cancel":true,"default_memory_log":true,"released":true,"construction_ticks":%llu,"qpc_frequency":%llu})" "\n",
      static_cast<unsigned long long>(hooks.construction_ticks),static_cast<unsigned long long>(frequency.QuadPart));
#else
    hooks.stage(1,0,0);hooks.begin_construction();hooks.ready();hooks.stage(2,0,0);
    auto call=[](int value){volatile int input=value;footprint::require(input+1==value+1);};
    for(int i=0;i<4;++i)hooks.measure("warmup",i,false,[&]{call(i);});
    hooks.stage(3,0,0);hooks.stage(4,0,0);
    for(int i=0;i<40;++i){hooks.measure("local_call",i,true,[&]{call(i);});hooks.measure("second_local_call",i,true,[&]{call(i);});}
    hooks.stage(5,0,0);hooks.stage(6,0,0);hooks.stage(7,0,0);hooks.stage(8,0,0);
    hooks.stage(9,0,0);hooks.stage(10,0,0);
#ifdef OCK_FOOTPRINT_ALLOCATION
    hooks.recorder.emit("baseline");
#endif
    std::printf(R"({"kind":"baseline","workers":0,"warmup":4,"local_calls":80,"released":true,"construction_ticks":%llu,"qpc_frequency":%llu})" "\n",
      static_cast<unsigned long long>(hooks.construction_ticks),static_cast<unsigned long long>(frequency.QuadPart));
#endif
#ifdef OCK_FOOTPRINT_ALLOCATION
    return hooks.recorder.verified()?0:1;
#else
    return 0;
#endif
  } catch(const std::exception& failure){std::fprintf(stderr,"%s\n",failure.what());return 1;}
}
