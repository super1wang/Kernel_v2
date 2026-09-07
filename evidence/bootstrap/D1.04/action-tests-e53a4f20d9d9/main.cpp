#include "tests/contract/authorization/action_cases.hpp"
int main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];
if(name=="permit_origin_binding"){policy_test::action_cases::permit_origin_binding();return 0;}
if(name=="permit_once"){policy_test::action_cases::permit_once();return 0;}
if(name=="permit_concurrent"){policy_test::action_cases::permit_concurrent();return 0;}
if(name=="revoke_before_consume"){policy_test::action_cases::revoke_before_consume();return 0;}
if(name=="consume_before_revoke"){policy_test::action_cases::consume_before_revoke();return 0;}
if(name=="cancel_arbitration"){policy_test::action_cases::cancel_arbitration();return 0;}
if(name=="group_members_complete"){policy_test::action_cases::group_members_complete();return 0;}
if(name=="group_substitution"){policy_test::action_cases::group_substitution();return 0;}
throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
