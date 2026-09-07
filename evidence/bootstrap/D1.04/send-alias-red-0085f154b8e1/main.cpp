#include "tests/contract/authorization/send_cases.hpp"
#include <cstdlib>
#include <new>
void* operator new(std::size_t n){if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {policy_test::authority_cases::observe_returned_buffer_free(p);std::free(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
int main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];
if(name=="transmission_start_arbitration"){policy_test::send_cases::transmission_start_arbitration();return 0;}
if(name=="failed_start_not_started"){policy_test::send_cases::failed_start_not_started();return 0;}
if(name=="unknown_start_no_retry"){policy_test::send_cases::unknown_start_no_retry();return 0;}
if(name=="unsubscribe_inflight"){policy_test::send_cases::unsubscribe_inflight();return 0;}
if(name=="subscription_connection_cleanup"){policy_test::send_cases::subscription_connection_cleanup();return 0;}
throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
