#pragma once
#include "value_cases.hpp"
namespace native_test {
class NativeProvider final : public AtomicProviderPort<Provider> {
public:
  Result<std::unique_ptr<Provider::Frame>> begin(const AtomicDomainRef&) override { throw std::runtime_error("unexpected provider begin"); }
  Result<std::shared_ptr<const PreparedCommit>> prepare(Provider::Frame&,const PreparedIdentity&) override { throw std::runtime_error("unexpected provider prepare"); }
  Result<void> commit(std::shared_ptr<const PreparedCommit>,std::shared_ptr<const ActionPermit>,std::shared_ptr<CommitReceiver>) override { throw std::runtime_error("unexpected provider commit"); }
};
inline void add_provider(registry::ModuleInput& m) {
  m.manifest.providers.push_back(name("provider"));
  m.manifest.required_providers.push_back({{name("native"),name("provider")},CppTypeToken::of<Provider>()});
  std::shared_ptr<AtomicProviderPort<Provider>> owner=std::make_shared<NativeProvider>();
  m.providers.push_back(*registry::ProviderBinding::make<Provider>(owner));
}
inline std::atomic<unsigned> candidate_entered=0;
inline Result<int> candidate_read(const int&,const Provider::CandidateReadPort&,WorkContext&) { ++candidate_entered;return 999; }
inline void candidate_read_dispatch() {
  Env e(true,compute,{},[](registry::ModuleInput& m){add_provider(m);
    m.register_operations=[](registry::Registrar& r){auto options=native_options();
      options.read_service=registry::ServiceRef{name("native"),name("reader")};
      options.provider=registry::ProviderRef{name("native"),name("provider")};
      CHECK(r.candidate_read<int,int,Reader,Provider>(read,candidate_read,native_definition(AtomicMode::Incompatible),options));};});
  auto b=e.bind();CHECK(b);entered=0;candidate_entered=0;
  CHECK(result(b->invoke(3,e.options()))==10);CHECK(entered==1&&candidate_entered==0);
}
inline Result<int> never_edit(const int&,EditView<Provider>&,WorkContext&) {++entered;throw std::runtime_error("state handler entered");}
inline EffectReport<int> never_effect(const int&,EffectContext&) {++entered;throw std::runtime_error("effect handler entered");}
inline TransitionReport<int> never_transition(const int&,TransitionView&) {++entered;throw std::runtime_error("lifecycle handler entered");}
inline void provider_unavailable() {
  for(auto shape:{Shape::StateEdit,Shape::ExternalEffect,Shape::Lifecycle}) {
    Env e(false,compute,{},[shape](registry::ModuleInput& m){if(shape==Shape::StateEdit)add_provider(m);
      m.register_operations=[shape](registry::Registrar& r){auto d=native_definition(AtomicMode::Incompatible);auto o=native_options();
        if(shape==Shape::StateEdit){d.atomic_mode=AtomicMode::StateEdit;o.provider=registry::ProviderRef{name("native"),name("provider")};CHECK(r.state_edit(never_edit,d,o));}
        else if(shape==Shape::ExternalEffect)CHECK(r.external_effect(never_effect,d,o));
        else CHECK(r.lifecycle(never_transition,d,o));};});
    auto b=e.bind(shape);CHECK(b);entered=0;
    for(int n:{-1,1}){auto reply=b->invoke(n,e.options());CHECK(std::holds_alternative<Rejected>(reply));
      CHECK(std::get<Rejected>(reply).reason.code()==invocation_error(InvocationErrc::ProviderUnavailable).code());}
    CHECK(entered==0);
  }
  Env good;auto b=good.bind();CHECK(b);CHECK(result(b->invoke(1,good.options()))==3);
}
inline void resource_lifetime() {
  NativeBudget limited;limited.resources_per_binding=1;
  Env excessive(false,compute,limited,[](registry::ModuleInput& m){
    for(auto label:{"r1","r2"}){m.manifest.resources.push_back(name(label));m.manifest.required_resources.push_back({name("native"),name(label)});m.resources.push_back({name(label),std::make_shared<ResourceLease>()});}
    m.register_operations=[](registry::Registrar& r){auto o=native_options();o.resources={{name("native"),name("r1")},{name("native"),name("r2")}};CHECK(r.compute(compute,native_definition(),o));};});
  CHECK(!excessive.bind());
  auto owner=std::make_shared<ResourceLease>();std::weak_ptr<ResourceLease> weak=owner;
  {
    Env e(false,compute,{},[&](registry::ModuleInput& m){m.manifest.resources.push_back(name("resource"));
      m.manifest.required_resources.push_back({name("native"),name("resource")});m.resources.push_back({name("resource"),owner});
      m.register_operations=[](registry::Registrar& r){auto o=native_options();o.resources.push_back({name("native"),name("resource")});CHECK(r.compute(compute,native_definition(),o));};});
    auto b=e.bind();CHECK(b);owner.reset();e.catalog.reset();CHECK(!weak.expired());entered=0;
    auto r=b->invoke(-1,e.options());CHECK(std::holds_alternative<Rejected>(r));
    CHECK(std::get<Rejected>(r).reason.code()==invocation_error(InvocationErrc::ResourceUnavailable).code());CHECK(entered==0);
  }
  CHECK(weak.expired());
  auto resource=std::make_shared<ResourceLease>();const ResourceLease* view=resource.get();
  auto budget=*foundation::CheckedCount<std::uint64_t>::create(0,5);
  { WorkContext borrowed({},policy::TimePoint{},budget,name("borrowed"),BorrowedResourceViews{std::span(&view,1)});
    CHECK(borrowed.granted_resources().size()==1&&borrowed.granted_resources()[0]==resource.get()); }
  Env empty(false,budget_compute);auto b=empty.bind();CHECK(b);CHECK(result(b->invoke(0,empty.options()))==1);
}
inline void outcome_consistency() {
  Publication publication;OutcomeValidation validation{publication,{}, {0,0,0}};
  for(auto handler:{compute,business_error}) {
    Env e(false,handler);auto b=e.bind();CHECK(b);auto reply=b->invoke(0,e.options());
    CHECK(std::holds_alternative<Completed<int>>(reply));const auto& out=std::get<Completed<int>>(reply).outcome;
    CHECK(out.facts().values().empty());CHECK(out.evidence()==EvidenceState::Volatile);
    CHECK(out.revalidate(validation,out.conditions(),out.facts()));
    CHECK(out.conditions().before_apply&&out.conditions().before_apply->no_application_proven);
  }
}
inline void aggregate_budget() {
  Env e;NativeBudget enormous;enormous.bindings=(std::numeric_limits<std::size_t>::max)()/16;
  CHECK(!NativeEngine::create(e.catalog,e.policy.session,e.threads,enormous));
}
inline void native_invalidation() {
  Env e;auto b=e.bind();CHECK(b);CHECK(result(b->invoke(1,e.options()))==3);
  auto original=policy_test::configuration().principals[0];auto denied=original;denied.rules.clear();
  CHECK(e.policy.assembly.administration->replace_principal_policy(denied));entered=0;
  CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));CHECK(entered==0);
  CHECK(e.policy.assembly.administration->replace_principal_policy(original));
  CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));CHECK(entered==0);
  auto fresh=e.bind();CHECK(fresh);CHECK(result(fresh->invoke(1,e.options()))==3);
}
inline void lifecycle_replacement() {
  Env e;auto b=e.bind();CHECK(b);CHECK(e.policy.assembly.administration->set_lifecycle(policy_test::target(),2));entered=0;
  CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));CHECK(entered==0);
  auto fresh=e.bind();CHECK(fresh);CHECK(result(fresh->invoke(1,e.options()))==3);
  policy_inline_test::close_and_original_retention();
}
inline void admission_order() {
  Env e(false,callback_compute);auto b=e.bind();CHECK(b);entered=0;
  entered_hook=[&]{auto denied=policy_test::configuration().principals[0];denied.rules.clear();CHECK(e.policy.assembly.administration->replace_principal_policy(denied));};
  CHECK(result(b->invoke(0,e.options()))==1);entered_hook={};CHECK(entered==1);
  CHECK(std::holds_alternative<Rejected>(b->invoke(0,e.options())));CHECK(entered==1);
  native_invalidation();
}
}
