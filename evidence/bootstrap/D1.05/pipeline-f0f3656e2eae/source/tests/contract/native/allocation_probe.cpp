// D1.05 测量消费者的分配通道；不进入内核链接产物。
#include "allocation_probe.hpp"
#include <cstdlib>
#include <malloc.h>
#include <new>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
namespace native_test::allocation {
thread_local bool enabled=false;
thread_local Counts counts;
#ifdef _DEBUG
int hook(int kind,void*,std::size_t,int,long,const unsigned char*,int) {
  if(enabled && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC)) ++counts.crt;
  return TRUE;
}
#endif
void initialize() noexcept {
#ifdef _DEBUG
  _CrtSetAllocHook(hook);
#endif
}
void start() noexcept { counts={};enabled=true; }
Counts stop() noexcept { enabled=false;return counts; }
bool crt_available() noexcept {
#ifdef _DEBUG
  return true;
#else
  return false;
#endif
}
void note() noexcept {if(enabled)++counts.cpp;}
}
void* operator new(std::size_t size) {
  native_test::allocation::note();
  if(auto p=std::malloc(size?size:1))return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,std::align_val_t align) {
  native_test::allocation::note();
  if(auto p=_aligned_malloc(size?size:1,static_cast<std::size_t>(align)))return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t s,std::align_val_t a){return ::operator new(s,a);}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
void* operator new(std::size_t s,const std::nothrow_t&) noexcept {try{return ::operator new(s);}catch(...){return nullptr;}}
void* operator new[](std::size_t s,const std::nothrow_t&) noexcept {try{return ::operator new[](s);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
void* operator new(std::size_t s,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(s,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t s,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](s,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {_aligned_free(p);}
