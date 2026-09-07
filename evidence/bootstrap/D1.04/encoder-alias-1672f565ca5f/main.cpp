#include "tests/contract/authorization/fixtures.hpp"
#include <cstdlib>
thread_local std::uintptr_t watched=0;
thread_local bool freed=false;
void*operator new(std::size_t n){if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{if(watched&&reinterpret_cast<std::uintptr_t>(p)==watched)freed=true;std::free(p);}
void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void*p)noexcept{::operator delete(p);}
void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
using namespace policy_test;
struct AliasedEncoder final:ProjectionEncoderPort {
 std::byte* alias=nullptr;
 Result<std::vector<std::byte>> encode(const ProjectionSnapshot&,std::size_t)override{
  std::vector<std::byte> bytes{std::byte{0x51},std::byte{0x01}};
  alias=bytes.data();watched=reinterpret_cast<std::uintptr_t>(alias);freed=false;
  return std::move(bytes);
 }
};
int main(int argc,char**argv){try{
 CHECK(argc==2);bool mutate=std::string(argv[1])=="alias";
 {std::vector<std::byte> check(2);watched=reinterpret_cast<std::uintptr_t>(check.data());freed=false;}
 CHECK(freed);watched=0;
 Env env;auto encoder=std::make_shared<AliasedEncoder>();auto sink=std::make_shared<Sink>();
 bool mutation=false;
 sink->before_reserve_return=[&]{if(mutate&&!freed){encoder->alias[0]=std::byte{0x7e};mutation=true;}};
 auto send=SendCoordinator::create(env.session,sink,encoder);CHECK(send);
 auto response=env.session->observations()->get(*env.caller,{id<foundation::TaskId>()},AccessUse::GetSummary);CHECK(response);
 CHECK((*send)->enqueue_response(response->response));
 CHECK((*send)->start_next()==StartResult::Started);watched=0;
 std::cout<<"mutation="<<mutation<<" bytes="<<sink->size<<" first="<<std::to_integer<unsigned>(sink->data[0])<<"\n";
 require(sink->size==2&&sink->data[0]==std::byte{0x51},"encoded bytes remain independently owned");
 return 0;
 }catch(const std::exception&e){watched=0;std::cerr<<e.what()<<"\n";return 1;}}
