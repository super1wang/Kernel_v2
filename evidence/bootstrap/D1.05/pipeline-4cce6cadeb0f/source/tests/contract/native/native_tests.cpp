#include "fixtures.hpp"
#include "pipeline_cases.hpp"
#include "value_cases.hpp"
#include "policy_inline_cases.hpp"
using namespace native_test;
void registered_compute() { Env e;auto b=e.bind();CHECK(b);entered=0;CHECK(result(b->invoke(2,e.options()))==4);CHECK(result(b->invoke(5,e.options()))==7);CHECK(entered==2); }
void registered_read() { Env e(true);auto b=e.bind();CHECK(b);e.reader.reset();CHECK(result(b->invoke(3,e.options()))==10); }
void invalid_input() { Env e;auto b=e.bind();CHECK(b);entered=0;CHECK(std::holds_alternative<Rejected>(b->invoke(-1,e.options())));CHECK(entered==0);CHECK(result(b->invoke(0,e.options()))==2); value_invalid_input(); }
void authority_origin() { Env e;policy_test::Env other;CHECK(!e.engine->bind<int,int>(key(),{},Shape::Read,other.caller,std::array{policy_test::target()},targets,name("native.call")));CHECK(e.bind()); }
int main(int argc,char**argv) {
  const std::map<std::string,void(*)()> cases{{"T02.native.registered_compute",registered_compute},{"T02.native.registered_read",registered_read},{"T02.native.invalid_input",invalid_input},{"T06.native.authority_origin",authority_origin},
    {"T06.native.business_failure",business_failure},{"T03.native.no_task_path",no_task_path},
    {"T03.native.reentrant_and_concurrent",reentrant_and_concurrent},
    {"T02.native.exact_binding",exact_binding},{"T02.native.validation_exception",validation_exception},
    {"T06.native.invalid_output",invalid_output},{"T06.native.target_binding",target_binding},
    {"T03.native.thread_origin",thread_origin},{"T03.native.thread_modes",thread_modes},
    {"T03.native.cancel_and_budget",cancel_and_budget},
    {"T06.native.permission_intersection",policy_inline_test::required_permissions_and_tuple},
    {"T06.native.revoke_and_regrant",policy_inline_test::invalidation},
    {"T06.native.session_and_deadline",policy_inline_test::deadlines_slots_and_cancellation}};
  try {if(argc==2&&std::string_view(argv[1])=="--list"){for(auto&[n,f]:cases)std::cout<<n<<'\n';return 0;}if(argc!=2)return 2;auto i=cases.find(argv[1]);if(i==cases.end())return 2;i->second();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
