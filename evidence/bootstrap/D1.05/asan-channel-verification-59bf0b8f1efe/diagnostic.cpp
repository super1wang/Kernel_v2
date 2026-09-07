
#include "allocation_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <malloc.h>
#ifdef DIAG_ASAN
#include <sanitizer/allocator_interface.h>
thread_local bool observing=false;
thread_local size_t asan_allocs=0,asan_frees=0;
void on_alloc(const volatile void*,size_t){if(observing)++asan_allocs;}
void on_free(const volatile void*){if(observing)++asan_frees;}
#endif
int main(int argc,char**){if(argc>1)return native_test::allocation::registration_failure_probe();
 native_test::allocation::initialize();native_test::allocation::initialize();
#ifdef DIAG_ASAN
 int installed=__sanitizer_install_malloc_and_free_hooks(on_alloc,on_free);
 std::printf("installed_sanitizer_hooks=%d\n",installed);
#endif
 for(int kind=0;kind<12;++kind){
  void* seed=kind==10?std::malloc(37):nullptr;
  if(kind==10&&!seed)return 3;
#ifdef DIAG_ASAN
  asan_allocs=0;asan_frees=0;observing=true;
#endif
  native_test::allocation::start();void* p=nullptr;
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
  auto c=native_test::allocation::stop();
#ifdef DIAG_ASAN
  observing=false;
#endif
  const char* names[]={"new","new_array","aligned_new","aligned_array","nothrow_new","nothrow_array","nothrow_aligned","nothrow_aligned_array","malloc","calloc","realloc","aligned_malloc"};
  std::printf("%s cpp=%zu crt=%zu measured_asan=%zu measured_asan_frees=%zu",names[kind],c.cpp,c.crt,c.asan_allocations,c.asan_frees);
#ifdef DIAG_ASAN
  std::printf(" asan_allocs=%zu asan_frees=%zu",asan_allocs,asan_frees);
#endif
  std::printf(" actual_success=%d\n",bool(p));
 }
}
