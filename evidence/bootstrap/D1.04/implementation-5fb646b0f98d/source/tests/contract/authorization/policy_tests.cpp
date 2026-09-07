#include "fixtures.hpp"
#include "action_cases.hpp"
#include "session_cases.hpp"
using namespace policy_test;
std::size_t selector_text(const OperationSelector&o){return o.operation.name.view().size()+o.operation.version.text().size();}
std::size_t rule_text(const std::vector<ScopeRule>&rules){std::size_t n=0;for(auto&r:rules){if(r.operation)n+=selector_text(*r.operation);for(auto&p:r.permissions)n+=p.view().size();}return n;}
std::size_t configured_text(){auto c=configuration();std::size_t n=0;for(auto&p:c.principals)n+=rule_text(p.rules);for(auto&o:c.operations){n+=selector_text(o.operation)+rule_text(o.module_rules);for(auto&p:o.required_permissions)n+=p.view().size();}for(auto&u:c.uses){n+=rule_text(u.module_rules);for(auto&p:u.required_permissions)n+=p.view().size();}for(auto&t:c.targets)n+=rule_text(t.rules);return n+2*rule_text(rules());}
void budget_contract(){
  PolicyBudget tight;tight.text_bytes=configured_text()+2*selector_text(operation());Env probe(tight);CHECK(!probe.prepare());
  PolicyBudget enough;enough.text_bytes=configured_text()+4*selector_text(operation());Env exact(enough);
  auto action=exact.prepare();CHECK(action);auto permit=(*action)->issue();CHECK(permit);auto expected=(*action)->current_expected_binding();CHECK(expected);CHECK((*action)->consume(**permit,*expected));
  action->reset();auto next=exact.prepare();CHECK(next);CHECK(!(*next)->issue());permit->reset();CHECK((*next)->issue());
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
void page_owner_authorization(){Env e;ListRequest request{principal(),PhaseSet::All,{1,1},{}};auto page=e.session->observations()->list(*e.caller,request,{});CHECK(page);CHECK(page->page.items.size()==1);CHECK(page->continuation);auto next=e.session->observations()->list(*e.caller,request,page->continuation);CHECK(next&&next->page.items.size()==1);CHECK(next->page.items[0].listing_ordinal<page->page.items[0].listing_ordinal);auto denied=request;denied.owner=principal(3);auto before=e.source->scans;CHECK(!e.session->observations()->list(*e.caller,denied,{}));CHECK(e.source->scans==before);}
void subscription_scope_atomic(){Env e;ObservationFilter f{{{id<foundation::TaskId>()}}, {},{ObservationTopic::Progress}};auto watch=e.session->observations()->subscribe(*e.caller,f);CHECK(watch);auto bad=f;bad.executions.push_back({id<foundation::TaskId>(3)});CHECK(!e.session->observations()->subscribe(*e.caller,bad));auto sink=std::make_shared<Sink>();auto send=SendCoordinator::create(e.session,sink,std::make_shared<Encoder>());CHECK(send);CHECK((*send)->enqueue(**watch,{e.source->rows[0].second.summary,ObservationTopic::Progress,false}));CHECK((*send)->start_next()==StartResult::Started);CHECK(sink->size==2);auto removed=(*send)->unsubscribe(**watch);CHECK(removed&&*removed);CHECK(!(*send)->enqueue(**watch,{e.source->rows[0].second.summary,ObservationTopic::Progress,false}));}
#ifndef OCK_POLICY_STAGE
void queued_revoke_drop(){Env e;auto response=e.session->observations()->get(*e.caller,{id<foundation::TaskId>()},AccessUse::GetSummary);CHECK(response);auto sink=std::make_shared<Sink>();auto send=SendCoordinator::create(e.session,sink,std::make_shared<Encoder>());CHECK(send);CHECK((*send)->enqueue_response(response->response));CHECK(e.assembly.administration->close_store());CHECK(!(*send)->start_next());CHECK(sink->size==0);}
#endif
int main(int argc,char** argv){
  try{
    CHECK(argc==2);
    const std::map<std::string,void(*)()> cases{{"T07.policy.session_isolation",session_cases::session_isolation},{"T07.policy.service_principal",session_cases::service_principal},{"T07.policy.recursive_delegation_denied",session_cases::recursive_delegation_denied},{"T07.policy.delegation_shrink",session_cases::delegation_shrink},{"T07.policy.operation_exact_contract",session_cases::operation_exact_contract},{"T07.policy.target_frozen_set",session_cases::target_frozen_set},{"T07.policy.owned_inputs_budget",budget_contract},{"T07.policy.authentication_source",authentication_source},{"T07.policy.expiry_boundary",clock_boundaries},{"T07.policy.scope_four_way",scope_four_way},{"T07.policy.target_issued_identity",target_issued_identity},{"T07.policy.permit_once",action_cases::permit_once},{"T07.policy.revoke_before_consume",action_cases::revoke_before_consume},{"T07.policy.cancel_arbitration",action_cases::cancel_arbitration},{"T07.policy.permit_origin_binding",action_cases::permit_origin_binding},{"T07.policy.permit_concurrent",action_cases::permit_concurrent},{"T07.policy.consume_before_revoke",action_cases::consume_before_revoke},{"T07.policy.group_members_complete",action_cases::group_members_complete},{"T07.policy.group_substitution",action_cases::group_substitution},{"T19.policy.page_owner_authorization",page_owner_authorization},{"T19.policy.subscription_scope_atomic",subscription_scope_atomic}
#ifndef OCK_POLICY_STAGE
,{"T20.policy.queued_revoke_drop",queued_revoke_drop}
#endif
};
    if(std::string(argv[1])=="--list"){for(auto&[n,f]:cases)std::cout<<n<<'\n';return 0;}
    auto found=cases.find(argv[1]);CHECK(found!=cases.end());found->second();return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
