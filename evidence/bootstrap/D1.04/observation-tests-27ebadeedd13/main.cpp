#include "tests/contract/authorization/observation_cases.hpp"
int main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];
if(name=="noninvoke_permissions"){policy_test::observation_cases::noninvoke_permissions();return 0;}
if(name=="page_binding_dimensions"){policy_test::observation_cases::page_binding_dimensions();return 0;}
if(name=="source_store_lifetime"){policy_test::observation_cases::source_store_lifetime();return 0;}
throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
