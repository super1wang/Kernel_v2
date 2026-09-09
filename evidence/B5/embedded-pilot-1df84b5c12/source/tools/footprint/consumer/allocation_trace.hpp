#pragma once
#include "channel.hpp"
#include <array>
#include <cstdio>
#ifdef OCK_FOOTPRINT_ALLOCATION
#include "../../../tests/contract/native/allocation_probe.hpp"
#include <malloc.h>
#include <new>
#endif
namespace footprint {
#ifdef OCK_FOOTPRINT_ALLOCATION
namespace probe=native_test::allocation;
inline constexpr const char* counter_mode="allocation";
inline bool zero(probe::Counts c){return c.cpp==0&&(!probe::crt_available()||c.crt==0)&&(!probe::asan_available()||c.asan_allocations==0);}
class Window {
  bool active_=true;
public:
  Window(){probe::start();}
  ~Window(){if(active_)(void)probe::stop();}
  probe::Counts finish(){active_=false;return probe::stop();}
};
class Trace {
  struct Row{const char* label;unsigned index;probe::Counts counts;bool success;};
  std::array<Row,96> rows_{};std::size_t size_=0;bool verified_=true;
  void add(const char* label,unsigned index,probe::Counts c,bool success){
    require(size_<rows_.size());rows_[size_++]={label,index,c,success};verified_=verified_&&success;
  }
public:
  void initialize(){
    probe::initialize();probe::initialize();
    for(unsigned kind=0;kind<12;++kind){
      void* seed=kind==10?std::malloc(37):nullptr;if(kind==10)require(seed!=nullptr);
      Window window;void* p=nullptr;
      switch(kind){
        case 0:p=::operator new(37);::operator delete(p);break;
        case 1:p=::operator new[](37);::operator delete[](p);break;
        case 2:p=::operator new(64,std::align_val_t{64});::operator delete(p,std::align_val_t{64});break;
        case 3:p=::operator new[](64,std::align_val_t{64});::operator delete[](p,std::align_val_t{64});break;
        case 4:p=::operator new(37,std::nothrow);::operator delete(p,std::nothrow);break;
        case 5:p=::operator new[](37,std::nothrow);::operator delete[](p,std::nothrow);break;
        case 6:p=::operator new(64,std::align_val_t{64},std::nothrow);::operator delete(p,std::align_val_t{64},std::nothrow);break;
        case 7:p=::operator new[](64,std::align_val_t{64},std::nothrow);::operator delete[](p,std::align_val_t{64},std::nothrow);break;
        case 8:p=std::malloc(37);std::free(p);break;
        case 9:p=std::calloc(7,9);std::free(p);break;
        case 10:p=std::realloc(seed,128);std::free(p?p:seed);break;
        case 11:p=_aligned_malloc(64,64);_aligned_free(p);break;
      }
      auto c=window.finish();bool valid=p!=nullptr;
      valid=valid&&(kind<8?(c.cpp==1&&c.frees==1&&c.allocated_bytes==c.released_bytes&&c.live_bytes_before==c.live_bytes_after):(c.cpp==0&&c.frees==0));
      if(probe::asan_available())valid=valid&&c.asan_allocations==1&&c.crt==0;
      else if(probe::crt_available())valid=valid&&c.crt>0;
      add("probe",kind,c,valid);require(valid);
    }
    Window window;volatile unsigned number=7;number=number+2;auto counts=window.finish();
    const bool valid=number==9&&zero(counts);add("probe_negative",0,counts,valid);require(valid);
  }
  template<class F>void measure(const char* label,unsigned index,bool require_zero,F&& work){
    Window window;
    try{work();}
    catch(...){auto c=window.finish();add(label,index,c,false);throw;}
    auto c=window.finish();add(label,index,c,true);if(require_zero&&!zero(c))verified_=false;
  }
  bool verified()const{return verified_;}
  void emit(const char* kind,bool completed)const{
    std::printf("{\"format\":\"ock.footprint-allocation/1\",\"kind\":\"%s\",\"verified\":%s,\"coverage\":{\"cpp\":true,\"crt\":%s,\"asan\":%s,\"debug_crt_effective\":\"%s\",\"channels_added\":false,\"window\":\"complete Scenario.call result lifetime; HostBound for native, local computation for baseline\",\"retained_scope\":\"executable replacement new/delete usable bytes; not process PrivateUsage\",\"blind_spots\":[\"Release CRT malloc family\",\"private DLL and custom heaps\",\"other threads\"]},\"samples\":[",kind,(completed&&verified_)?"true":"false",probe::crt_available()?"true":"false",probe::asan_available()?"true":"false",!probe::crt_available()?"unavailable":probe::asan_available()?"unobserved_asan_intercepted":"full");
    for(std::size_t i=0;i<size_;++i){const auto& r=rows_[i];const auto& c=r.counts;if(i)std::putchar(',');
      std::printf("{\"window\":\"%s\",\"index\":%u,\"success\":%s,\"cpp\":%zu,\"crt\":",r.label,r.index,r.success?"true":"false",c.cpp);
      if(probe::crt_available())std::printf("%zu",c.crt);else std::printf("null");
      std::printf(",\"asan_allocations\":");if(probe::asan_available())std::printf("%zu",c.asan_allocations);else std::printf("null");
      std::printf(",\"asan_frees\":");if(probe::asan_available())std::printf("%zu",c.asan_frees);else std::printf("null");
      std::printf(",\"cpp_frees\":%zu,\"allocated_bytes\":%zu,\"released_bytes\":%zu,\"live_before\":%zu,\"live_after\":%zu,\"peak_live\":%zu}",c.frees,c.allocated_bytes,c.released_bytes,c.live_bytes_before,c.live_bytes_after,c.peak_live_bytes);
    }std::puts("]}");
  }
};
inline void inject(unsigned index){
#ifdef OCK_FOOTPRINT_INJECT_ALLOCATION
  if(index==0){void* volatile allocated=::operator new(37);::operator delete(allocated);}
#else
  (void)index;
#endif
}
#else
inline constexpr const char* counter_mode="disabled";
class Trace {
public:
  void initialize(){}
  template<class F>void measure(const char*,unsigned,bool,F&& work){work();}
  bool verified()const{return true;}
  void emit(const char*,bool)const{}
};
inline void inject(unsigned){}
#endif
}
