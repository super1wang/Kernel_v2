#include "tests/contract/authorization/session_cases.hpp"
int main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];
if(name=="session_isolation"){policy_test::session_cases::session_isolation();return 0;}
if(name=="service_principal"){policy_test::session_cases::service_principal();return 0;}
if(name=="recursive_delegation_denied"){policy_test::session_cases::recursive_delegation_denied();return 0;}
if(name=="delegation_shrink"){policy_test::session_cases::delegation_shrink();return 0;}
if(name=="operation_exact_contract"){policy_test::session_cases::operation_exact_contract();return 0;}
if(name=="target_frozen_set"){policy_test::session_cases::target_frozen_set();return 0;}
throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
