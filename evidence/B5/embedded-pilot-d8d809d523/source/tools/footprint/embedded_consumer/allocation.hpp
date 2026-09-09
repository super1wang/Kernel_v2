#pragma once
#include "../consumer/allocation_trace.hpp"
#include <atomic>
#include <thread>
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/allocator_interface.h>
#elif defined(_DEBUG)
#include <crtdbg.h>
#else
#error Embedded allocation diagnostic requires Debug CRT or ASan
#endif
namespace embedded_allocation {
inline std::atomic<bool> active{false};
inline std::atomic<std::size_t> allocations{0};
#ifdef __SANITIZE_ADDRESS__
inline constexpr const char* channel="ASan";
inline void allocated(const volatile void*,std::size_t){if(active.load())++allocations;}
inline void freed(const volatile void*){}
inline void install(){footprint::require(__sanitizer_install_malloc_and_free_hooks(allocated,freed)!=0);}
#else
inline constexpr const char* channel="DebugCRT";
inline _CRT_ALLOC_HOOK previous=nullptr;
inline int hook(int kind,void* data,std::size_t size,int block,long request,const unsigned char* file,int line) {
  if(active.load()&&(kind==_HOOK_ALLOC||kind==_HOOK_REALLOC))++allocations;
  return previous?previous(kind,data,size,block,request,file,line):1;
}
inline void install(){previous=_CrtSetAllocHook(hook);}
#endif
class Recorder {
  struct Row {const char* label;unsigned index;native_test::allocation::Counts local;std::size_t process_crt;bool zero_required;};
  std::array<Row,96> rows_{};
  std::size_t size_=0,positive_=0;
  bool verified_=true;
  footprint::Trace probes_;
public:
  void initialize() {
    probes_.initialize();install();
    std::atomic<bool> ready=false,start=false,done=false,valid=false;
    std::jthread worker([&](std::stop_token stop){
      ready=true;
      while(!start&&!stop.stop_requested())std::this_thread::yield();
      if(stop.stop_requested())return;
      void* volatile pointer=std::malloc(37);valid=pointer!=nullptr;std::free(pointer);done=true;
      while(!stop.stop_requested())std::this_thread::yield();
    });
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!ready&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    footprint::require(ready);
    allocations=0;active=true;start=true;
    while(!done&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    active=false;positive_=allocations.load();
    footprint::require(done&&valid&&positive_>0);
    allocations=0;active=true;volatile unsigned number=7;number=number+2;active=false;
    footprint::require(number==9&&allocations==0);
    worker.request_stop();worker.join();
  }
  template<class F> void measure(const char* label,unsigned index,bool zero_required,F&& work) {
    footprint::require(size_<rows_.size());
    native_test::allocation::start();allocations=0;active=true;
    try {work();}catch(...){active=false;(void)native_test::allocation::stop();throw;}
    active=false;const auto process=allocations.load();const auto local=native_test::allocation::stop();
    rows_[size_++]={label,index,local,process,zero_required};
    if(zero_required&&(local.cpp!=0||process!=0))verified_=false;
  }
  bool verified()const{return verified_;}
  void emit(const char* kind)const {
    probes_.emit(kind,true);
    std::printf(R"({"format":"ock.embedded-allocation/2","kind":"%s","verified":%s,"process_channel":"%s","background_positive":%zu,"negative":0,"coverage":"executable C++ on caller thread plus named allocator hook on all threads; private DLL/custom heaps excluded","samples":[)",kind,verified_?"true":"false",channel,positive_);
    for(std::size_t i=0;i<size_;++i){const auto& r=rows_[i];if(i)std::putchar(',');
      std::printf(R"({"window":"%s","index":%u,"zero_required":%s,"cpp":%zu,"process_allocations":%zu,"cpp_frees":%zu,"live_before":%zu,"live_after":%zu,"peak_live":%zu})",
        r.label,r.index,r.zero_required?"true":"false",r.local.cpp,r.process_crt,r.local.frees,
        r.local.live_bytes_before,r.local.live_bytes_after,r.local.peak_live_bytes);
    }std::puts("]}");
  }
};
}
