#include "tests/contract/authorization/authority_cases.hpp"
#include <cstdlib>
void* operator new(std::size_t n){if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{policy_test::authority_cases::observe_returned_buffer_free(p);std::free(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
int main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];
if(name=="authentication_source"){policy_test::authority_cases::authentication_source();return 0;}
if(name=="scope_four_way"){policy_test::authority_cases::scope_four_way();return 0;}
if(name=="inputs_ownership"){policy_test::authority_cases::inputs_ownership();return 0;}
throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
