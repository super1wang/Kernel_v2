// D1.05 测量消费者的分配通道；不进入内核链接产物。
#include "allocation_probe.hpp"
#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
namespace native_test::allocation {
thread_local bool enabled=false;
thread_local Counts counts;
// 固定记账状态，不分配索引；仅涵盖本可执行程序替换 new/delete 的配对块。
// 使用 CRT usable-size，不声称包括堆元数据、DLL 或未经过替换入口的分配。
constinit std::atomic<std::size_t> live_bytes{0},live_blocks{0};
#ifdef _DEBUG
int hook(int kind,void*,std::size_t,int,long,const unsigned char*,int) {
  if(enabled && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC)) ++counts.crt;
  return 1;
}
#endif
void initialize() noexcept {
#ifdef _DEBUG
  _CrtSetAllocHook(hook);
#endif
}
void start() noexcept {
  counts={};
  counts.live_bytes_before=counts.peak_live_bytes=live_bytes.load(std::memory_order_relaxed);
  counts.live_blocks_before=live_blocks.load(std::memory_order_relaxed);
  enabled=true;
}
Counts stop() noexcept {
  enabled=false;
  counts.live_bytes_after=live_bytes.load(std::memory_order_relaxed);
  counts.live_blocks_after=live_blocks.load(std::memory_order_relaxed);
  return counts;
}
bool crt_available() noexcept {
#ifdef _DEBUG
  return true;
#else
  return false;
#endif
}
void allocated(std::size_t bytes) noexcept {
  const auto retained=live_bytes.fetch_add(bytes,std::memory_order_relaxed)+bytes;
  live_blocks.fetch_add(1,std::memory_order_relaxed);
  if(enabled){++counts.cpp;counts.allocated_bytes+=bytes;
    if(retained>counts.peak_live_bytes)counts.peak_live_bytes=retained;}
}
void released(std::size_t bytes) noexcept {
  const auto before=live_bytes.fetch_sub(bytes,std::memory_order_relaxed);
  const auto blocks=live_blocks.fetch_sub(1,std::memory_order_relaxed);
  if(before<bytes||!blocks)std::abort();
  if(enabled){++counts.frees;counts.released_bytes+=bytes;}
}
void release(void* p) noexcept {
  if(p){released(_msize(p));std::free(p);}
}
void release_aligned(void* p,std::align_val_t alignment) noexcept {
  if(p){released(_aligned_msize(p,static_cast<std::size_t>(alignment),0));_aligned_free(p);}
}
}
void* operator new(std::size_t size) {
  if(auto p=std::malloc(size?size:1)){native_test::allocation::allocated(_msize(p));return p;}
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {native_test::allocation::release(p);}
void operator delete[](void* p) noexcept {native_test::allocation::release(p);}
void operator delete(void* p,std::size_t) noexcept {native_test::allocation::release(p);}
void operator delete[](void* p,std::size_t) noexcept {native_test::allocation::release(p);}
void* operator new(std::size_t size,std::align_val_t align) {
  if(auto p=_aligned_malloc(size?size:1,static_cast<std::size_t>(align))){
    native_test::allocation::allocated(_aligned_msize(p,static_cast<std::size_t>(align),0));return p;}
  throw std::bad_alloc{};
}
void* operator new[](std::size_t s,std::align_val_t a){return ::operator new(s,a);}
void operator delete(void* p,std::align_val_t a) noexcept {native_test::allocation::release_aligned(p,a);}
void operator delete[](void* p,std::align_val_t a) noexcept {native_test::allocation::release_aligned(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {native_test::allocation::release_aligned(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {native_test::allocation::release_aligned(p,a);}
void* operator new(std::size_t s,const std::nothrow_t&) noexcept {try{return ::operator new(s);}catch(...){return nullptr;}}
void* operator new[](std::size_t s,const std::nothrow_t&) noexcept {try{return ::operator new[](s);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {native_test::allocation::release(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {native_test::allocation::release(p);}
void* operator new(std::size_t s,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(s,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t s,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](s,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept {native_test::allocation::release_aligned(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept {native_test::allocation::release_aligned(p,a);}
