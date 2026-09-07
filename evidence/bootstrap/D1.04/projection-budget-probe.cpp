#include "tests/contract/authorization/fixtures.hpp"
using namespace policy_test;
std::size_t selector_text(const OperationSelector&o){return o.operation.name.view().size()+o.operation.version.text().size();}
std::size_t rule_text(const std::vector<ScopeRule>&rules){std::size_t n=0;for(auto&r:rules){if(r.operation)n+=selector_text(*r.operation);for(auto&p:r.permissions)n+=p.view().size();}return n;}
std::size_t base_text(){auto c=configuration();std::size_t n=0;for(auto&p:c.principals)n+=rule_text(p.rules);for(auto&o:c.operations){n+=selector_text(o.operation)+rule_text(o.module_rules);for(auto&p:o.required_permissions)n+=p.view().size();}for(auto&u:c.uses){n+=rule_text(u.module_rules);for(auto&p:u.required_permissions)n+=p.view().size();}for(auto&t:c.targets)n+=rule_text(t.rules);return n+2*rule_text(rules());}
int main(int argc,char**argv){try{
 CHECK(argc==2);bool control=std::string(argv[1])=="control";
 PolicyBudget budget;const auto base=base_text(),key=selector_text(operation());budget.text_bytes=base+(control?2:1)*key;
 Env e(budget);auto response=e.session->observations()->get(*e.caller,{id<foundation::TaskId>()},AccessUse::GetSummary);
 std::cout<<"base="<<base<<" key="<<key<<" budget="<<budget.text_bytes<<" get="<<bool(response)<<"\n";
 if(control)require(bool(response),"two new key copies fit exactly");
 else require(!response,"one key budget cannot retain entry key and projection key");
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
