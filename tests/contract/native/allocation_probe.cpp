// D1.05 测量消费者的分配通道；不进入内核链接产物。
#include "allocation_probe.hpp"
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <malloc.h>
#include <new>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/allocator_interface.h>
#endif
namespace native_test::allocation {
thread_local bool enabled=false;
thread_local Counts counts;
// 固定记账状态，不分配索引；仅涵盖本可执行程序替换 new/delete 的配对块。
// 使用 CRT usable-size，不声称包括堆元数据、DLL 或未经过替换入口的分配。
constinit std::atomic<std::size_t> live_bytes{0},live_blocks{0};
bool initialized=false;
// ASan 可能在系统线程 TLS 尚未建立或已销毁时调用 hook。
// 先读无 TLS 的固定 owner，只有显式计数窗口的线程才能接触 thread_local。
constinit std::atomic<DWORD> counter_thread{0};
#ifdef __SANITIZE_ADDRESS__
void asan_allocate(const volatile void*,std::size_t) {
  if(counter_thread.load(std::memory_order_relaxed)==GetCurrentThreadId()&&enabled)++counts.asan_allocations;
}
void asan_free(const volatile void*) {
  if(counter_thread.load(std::memory_order_relaxed)==GetCurrentThreadId()&&enabled)++counts.asan_frees;
}
void unused_allocate(const volatile void*,std::size_t) {}
void unused_free(const volatile void*) {}
#endif
#ifdef _DEBUG
int hook(int kind,void*,std::size_t,int,long,const unsigned char*,int) {
  if(counter_thread.load(std::memory_order_relaxed)==GetCurrentThreadId()&&enabled && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC)) ++counts.crt;
  return 1;
}
#endif
void initialize() noexcept {
  // 仅由验证消费者主线程在测量和业务开始前调用；重复初始化不重复注册。
  if(initialized)return;
#ifdef __SANITIZE_ADDRESS__
  if(!__sanitizer_install_malloc_and_free_hooks(asan_allocate,asan_free)){
    std::fputs("native_asan_hook_registration_failed\n",stderr);
    std::fflush(stderr);std::_Exit(87);
  }
#endif
#ifdef _DEBUG
  _CrtSetAllocHook(hook);
#endif
  initialized=true;
}
bool asan_available() noexcept {
#ifdef __SANITIZE_ADDRESS__
  return initialized;
#else
  return false;
#endif
}
int registration_failure_probe() noexcept {
#ifdef __SANITIZE_ADDRESS__
  // 锁定公开头声明最多5对hook；有限8次尝试容纳已占用槽并验证真实失败。
  if(initialized)return 2;
  for(unsigned attempt=0;attempt<8;++attempt)
    if(!__sanitizer_install_malloc_and_free_hooks(unused_allocate,unused_free)){
      initialize(); // 真实槽满必须走注册失败路径退出87。
      return 2;
    }
  std::fputs("native_asan_hook_capacity_not_exhausted\n",stderr);
  std::fflush(stderr);return 2;
#else
  return 2;
#endif
}
void start() noexcept {
  counts={};
  counts.live_bytes_before=counts.peak_live_bytes=live_bytes.load(std::memory_order_relaxed);
  counts.live_blocks_before=live_blocks.load(std::memory_order_relaxed);
  enabled=true;
  counter_thread.store(GetCurrentThreadId(),std::memory_order_relaxed);
}
Counts stop() noexcept {
  counter_thread.store(0,std::memory_order_relaxed);
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
