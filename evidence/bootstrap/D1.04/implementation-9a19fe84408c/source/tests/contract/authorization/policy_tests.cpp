#include "fixtures.hpp"
using namespace policy_test;
void budget_contract(){
  PolicyBudget b;
  CHECK(b.sessions>0);
  CHECK(b.identity_limit>0);
}
int main(int argc,char** argv){
  try{
    CHECK(argc==2);
    if(std::string(argv[1])=="--list"){std::cout<<"T07.policy.owned_inputs_budget\n";return 0;}
    CHECK(std::string(argv[1])=="T07.policy.owned_inputs_budget");
    budget_contract();return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
