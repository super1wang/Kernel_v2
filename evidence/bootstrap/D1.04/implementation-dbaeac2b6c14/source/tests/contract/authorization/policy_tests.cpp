#include "fixtures.hpp"
using namespace policy_test;
void budget_contract(){
  PolicyBudget b;b.declarations=5000;Env e(b);
  DelegationInput repeated{{},e.auth->identity.deadline,false};
  repeated.rules.assign(200,rules().front());
  CHECK(!e.session->restrict_delegation(repeated));
  CHECK(e.prepare());
  repeated.rules.assign(40,rules().front());CHECK(e.session->restrict_delegation(repeated));
  CHECK(e.session->restrict_delegation({{},e.auth->identity.deadline,false}));
  auto another=e.assembly.store->open({{std::byte{7}}},{rules(),e.auth->identity.deadline,false});CHECK(another);
}
void clock_boundaries(){
  auto clock=std::make_shared<Clock>();clock->base=TimePoint(TimePoint::duration(-1));
  auto auth=std::make_shared<Auth>(clock->now()+std::chrono::hours(1));
  auto created=PolicyStore::create({},configuration(),auth,clock,std::make_shared<Digest>(),std::make_shared<Source>());CHECK(created);
  clock->base=TimePoint::max()-std::chrono::seconds(1);
  CHECK(!PolicyStore::create({},configuration(),auth,clock,std::make_shared<Digest>(),std::make_shared<Source>()));
  clock->base=TimePoint{};PolicyBudget b;b.session_ttl=std::chrono::milliseconds::max();
  CHECK(!PolicyStore::create(b,configuration(),auth,clock,std::make_shared<Digest>(),std::make_shared<Source>()));
}
void authentication_source(){Env e;CHECK(e.caller->view().revalidate());CHECK(!e.assembly.store->open({{std::byte{8}}},{rules(),e.auth->identity.deadline,false}));CHECK(!e.session->verify({principal(2),{}, {}}));}
void target_issued_identity(){
 auto cfg=configuration();cfg.targets[1].rules.clear();Env e({},cfg);auto a=e.session->targets()->resolve(e.caller->view(),target());CHECK(a);auto b=e.session->targets()->resolve(e.caller->view(),target(2));auto c=e.session->targets()->resolve(e.caller->view(),target(3));CHECK(!c);
 auto read=configuration();for(auto&r:read.principals)std::erase_if(r.rules,[](auto&x){return x.use!=AccessUse::ReadResult;});Env reader({},read);CHECK(reader.session->targets()->resolve(reader.caller->view(),target()));
 auto crossed=configuration();std::erase_if(crossed.principals[0].rules,[](auto&r){return r.use!=AccessUse::ReadResult;});std::erase_if(crossed.targets[0].rules,[](auto&r){return r.use!=AccessUse::GetSummary;});Env mismatch({},crossed);auto denied=mismatch.session->targets()->resolve(mismatch.caller->view(),target());CHECK(!denied);
 CHECK(!b);CHECK(b.error().code()==c.error().code());
}
void scope_four_way(){Env good;CHECK(good.prepare());for(int side=0;side<4;++side){auto cfg=configuration();if(side==0)cfg.principals[0].rules.clear();if(side==2)cfg.operations[0].module_rules.clear();if(side==3)cfg.targets[0].rules.clear();Env e({},cfg);if(side==1)CHECK(e.session->restrict_delegation({{},e.auth->identity.deadline,false}));CHECK(!e.prepare());}}
void permit_once(){Env e;auto a=e.prepare();CHECK(a);auto p=(*a)->issue();CHECK(p);auto again=(*a)->issue();CHECK(again&&again->get()==p->get());auto expected=(*a)->current_expected_binding();CHECK(expected);CHECK((*a)->consume(**p,*expected));CHECK(!(*a)->consume(**p,*expected));CHECK(!(*a)->issue());}
void revoke_before_consume(){Env e;auto a=e.prepare();CHECK(a);auto p=(*a)->issue();CHECK(p);auto expected=(*a)->current_expected_binding();CHECK(expected);CHECK(e.assembly.administration->replace_principal_policy({principal(),{}}));CHECK(!(*a)->consume(**p,*expected));}
void cancel_arbitration(){Env e;auto a=e.prepare();CHECK(a);auto p=(*a)->issue();CHECK(p);auto expected=(*a)->current_expected_binding();CHECK(expected);CHECK((*a)->cancel());CHECK(!(*a)->consume(**p,*expected));CHECK(!(*a)->issue());}
#ifndef OCK_POLICY_STAGE
void queued_revoke_drop(){Env e;auto response=e.session->observations()->get(*e.caller,{id<foundation::TaskId>()},AccessUse::GetSummary);CHECK(response);auto sink=std::make_shared<Sink>();auto send=SendCoordinator::create(e.session,sink,std::make_shared<Encoder>());CHECK(send);CHECK((*send)->enqueue_response(response->response));CHECK(e.assembly.administration->close_store());CHECK(!(*send)->start_next());CHECK(sink->size==0);}
#endif
int main(int argc,char** argv){
  try{
    CHECK(argc==2);
    const std::map<std::string,void(*)()> cases{{"T07.policy.owned_inputs_budget",budget_contract},{"T07.policy.authentication_source",authentication_source},{"T07.policy.expiry_boundary",clock_boundaries},{"T07.policy.scope_four_way",scope_four_way},{"T07.policy.target_issued_identity",target_issued_identity},{"T07.policy.permit_once",permit_once},{"T07.policy.revoke_before_consume",revoke_before_consume},{"T07.policy.cancel_arbitration",cancel_arbitration}
#ifndef OCK_POLICY_STAGE
,{"T20.policy.queued_revoke_drop",queued_revoke_drop}
#endif
};
    if(std::string(argv[1])=="--list"){for(auto&[n,f]:cases)std::cout<<n<<'\n';return 0;}
    auto found=cases.find(argv[1]);CHECK(found!=cases.end());found->second();return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
