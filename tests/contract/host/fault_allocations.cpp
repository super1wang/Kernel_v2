#include "fault_allocations.hpp"
#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>
namespace {
constinit std::atomic<std::int64_t> blocks{0};
thread_local bool measuring=false;
thread_local std::size_t ordinal=0, fail_at=0, failed=0, failed_size=0;
void* allocate(std::size_t size,std::size_t alignment=0) {
  if(measuring) {
    ++ordinal;
    if(fail_at && ordinal==fail_at) {
      fail_at=0;failed=ordinal;failed_size=size;
      throw std::bad_alloc();
    }
  }
  void* p=alignment?_aligned_malloc(size?size:1,alignment):std::malloc(size?size:1);
  if(!p)throw std::bad_alloc();
  blocks.fetch_add(1,std::memory_order_relaxed);return p;
}
void release(void* p,bool aligned=false) noexcept {
  if(!p)return;
  blocks.fetch_sub(1,std::memory_order_relaxed);
  if(aligned)_aligned_free(p);else std::free(p);
}
}
namespace host_fault {
void begin_allocations(std::size_t target) noexcept {ordinal=failed=failed_size=0;fail_at=target;measuring=true;}
AllocationObservation end_allocations() noexcept {
  measuring=false;fail_at=0;return {ordinal,failed,failed_size,live_blocks()};
}
std::int64_t live_blocks() noexcept {return blocks.load(std::memory_order_relaxed);}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void* operator new(std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return allocate(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return allocate(n);}catch(...){return nullptr;}}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}
void operator delete(void* p) noexcept{release(p);}
void operator delete[](void* p) noexcept{release(p);}
void operator delete(void* p,std::size_t) noexcept{release(p);}
void operator delete[](void* p,std::size_t) noexcept{release(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept{release(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{release(p);}
void operator delete(void* p,std::align_val_t) noexcept{release(p,true);}
void operator delete[](void* p,std::align_val_t) noexcept{release(p,true);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept{release(p,true);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept{release(p,true);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept{release(p,true);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept{release(p,true);}
