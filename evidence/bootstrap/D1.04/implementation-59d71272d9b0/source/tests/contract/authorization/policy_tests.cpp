#include "fixtures.hpp"
using namespace policy_test;
void budget_contract(){
  PolicyBudget b;
  CHECK(b.sessions>0);
  CHECK(b.identity_limit>0);
}
void authentication_source(){Env e;CHECK(e.caller->view().revalidate());CHECK(!e.assembly.store->open({{std::byte{8}}},{rules(),e.auth->identity.deadline,false}));CHECK(!e.session->verify({principal(2),{}, {}}));}
void scope_four_way(){Env good;CHECK(good.prepare());for(int side=0;side<4;++side){auto cfg=configuration();if(side==0)cfg.principals[0].rules.clear();if(side==2)cfg.operations[0].module_rules.clear();if(side==3)cfg.targets[0].rules.clear();Env e({},cfg);if(side==1)CHECK(e.session->restrict_delegation({{},e.auth->identity.deadline,false}));CHECK(!e.prepare());}}
#ifndef OCK_POLICY_STAGE
void queued_revoke_drop(){Env e;auto response=e.session->observations()->get(*e.caller,{id<foundation::TaskId>()},AccessUse::GetSummary);CHECK(response);auto sink=std::make_shared<Sink>();auto send=SendCoordinator::create(e.session,sink,std::make_shared<Encoder>());CHECK(send);CHECK((*send)->enqueue_response(response->response));CHECK(e.assembly.administration->close_store());CHECK(!(*send)->start_next());CHECK(sink->size==0);}
#endif
int main(int argc,char** argv){
  try{
    CHECK(argc==2);
    const std::map<std::string,void(*)()> cases{{"T07.policy.owned_inputs_budget",budget_contract},{"T07.policy.authentication_source",authentication_source},{"T07.policy.scope_four_way",scope_four_way}
#ifndef OCK_POLICY_STAGE
,{"T20.policy.queued_revoke_drop",queued_revoke_drop}
#endif
};
    if(std::string(argv[1])=="--list"){for(auto&[n,f]:cases)std::cout<<n<<'\n';return 0;}
    auto found=cases.find(argv[1]);CHECK(found!=cases.end());found->second();return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
