#include "policy.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <deque>
namespace ock::runtime::policy {
namespace detail {
template<class T>bool has(const std::vector<T>&v,const T&x){return std::find(v.begin(),v.end(),x)!=v.end();}
template<class T>bool unique(const std::vector<T>&v){for(std::size_t i=0;i<v.size();++i)for(std::size_t j=0;j<i;++j)if(v[i]==v[j])return false;return true;}
template<class T>bool subset(const std::vector<T>&a,const std::vector<T>&b){for(auto&x:a)if(!has(b,x))return false;return true;}
template<class T>bool owned(const std::shared_ptr<T>&p){return p&&p.use_count()>0;}
template<class T>Result<T> failure(PolicyErrc e){return make_unexpected(policy_error(e));}
struct Usage{std::size_t declarations=0,text=0;};
bool charge(std::size_t&used,std::size_t extra,std::size_t limit){if(extra>limit-used)return false;used+=extra;return true;}
struct Meter {
 const PolicyBudget&b;Usage value;bool valid=true;
 void count(std::size_t n){valid=charge(value.declarations,n,b.declarations)&&valid;}
 void text(std::string_view s){valid=charge(value.text,s.size(),b.text_bytes)&&valid;}
 void selector(const OperationSelector&o){text(o.operation.name.view());text(o.operation.version.text());}
 void rules(const std::vector<ScopeRule>&v){count(v.size());if(v.size()>b.rules){valid=false;return;}for(auto&r:v){if(r.use>AccessUse::Subscribe||(r.use==AccessUse::Invoke&&!r.operation))valid=false;if(r.operation)selector(*r.operation);count(r.permissions.size());count(r.targets.size());count(r.owners.size());count(r.fields.size());for(auto&n:r.permissions)text(n.view());if(!unique(r.permissions)||!unique(r.targets)||!unique(r.owners)||!unique(r.fields))valid=false;for(auto t:r.targets)if(t.empty())valid=false;for(auto o:r.owners)if(o.principal_id.empty())valid=false;for(auto f:r.fields)if(f>SummaryField::Facts)valid=false;}}
};
bool valid_budget(const PolicyBudget&b){for(auto n:{b.principals,b.rules,b.targets,b.sessions,b.active_actions,b.active_responses,b.active_watches,b.members,b.watches_per_session,b.watches_per_principal,b.diagnostics,b.declarations,b.text_bytes,b.credential_bytes,b.queued_frames,b.queued_bytes,b.frame_bytes,b.page_size,b.scan_limit})if(!n)return false;return b.identity_limit&&b.generation_limit&&b.page_size<=200&&b.frame_bytes<=b.queued_bytes&&b.session_ttl.count()>0&&b.action_ttl.count()>0&&b.page_ttl.count()>0&&b.queued_ttl.count()>0;}
Result<TimePoint> expires(TimePoint now,std::chrono::milliseconds ttl){auto count=ttl.count();using D=TimePoint::duration;if(count<=0||count>(std::numeric_limits<D::rep>::max)()/1000000)return failure<TimePoint>(PolicyErrc::InvalidInput);auto delta=std::chrono::duration_cast<D>(ttl);if(delta>TimePoint::max()-now)return failure<TimePoint>(PolicyErrc::InvalidInput);return now+delta;}
Usage config_usage(const PolicyConfiguration&c,const PolicyBudget&b,bool&good){Meter m{b};if(c.principals.size()>b.principals||c.targets.size()>b.targets||c.operations.size()>b.rules||c.uses.size()>10)m.valid=false;m.count(c.principals.size());m.count(c.operations.size());m.count(c.uses.size());m.count(c.targets.size());
 for(std::size_t i=0;i<c.principals.size();++i){auto&p=c.principals[i];if(p.principal.principal_id.empty())m.valid=false;for(std::size_t j=0;j<i;++j)if(c.principals[j].principal==p.principal)m.valid=false;m.rules(p.rules);}
 for(std::size_t i=0;i<c.operations.size();++i){auto&o=c.operations[i];m.selector(o.operation);m.count(o.required_permissions.size());for(auto&n:o.required_permissions)m.text(n.view());if(o.required_permissions.empty()||!unique(o.required_permissions))m.valid=false;for(std::size_t j=0;j<i;++j)if(c.operations[j].operation.operation==o.operation.operation)m.valid=false;m.rules(o.module_rules);}
 for(std::size_t i=0;i<c.uses.size();++i){auto&u=c.uses[i];if(u.use==AccessUse::Invoke||u.use>AccessUse::Subscribe||u.required_permissions.empty()||!unique(u.required_permissions))m.valid=false;for(std::size_t j=0;j<i;++j)if(c.uses[j].use==u.use)m.valid=false;m.count(u.required_permissions.size());for(auto&n:u.required_permissions)m.text(n.view());m.rules(u.module_rules);}
 for(std::size_t i=0;i<c.targets.size();++i){auto&t=c.targets[i];if(t.target.empty()||!t.lifecycle_generation||t.lifecycle_generation>b.generation_limit||!owned(t.lifetime_owner)||dynamic_cast<ResourceLease*>(t.lifetime_owner.get())||dynamic_cast<ActivityLease*>(t.lifetime_owner.get()))m.valid=false;for(std::size_t j=0;j<i;++j)if(c.targets[j].target==t.target)m.valid=false;m.rules(t.rules);}good=m.valid;return m.value;
}
std::atomic<std::uint64_t> store_sequence{0};
Result<std::uint64_t> issue_process(){auto n=store_sequence.load();for(;;){if(n==(std::numeric_limits<std::uint64_t>::max)())return failure<std::uint64_t>(PolicyErrc::IdentityExhausted);if(store_sequence.compare_exchange_weak(n,n+1))return n+1;}}
template<class T>T identity(std::uint64_t store,std::uint64_t child){T result{};for(unsigned i=0;i<8;++i){result.bytes[7-i]=static_cast<std::uint8_t>(store>>(i*8));result.bytes[15-i]=static_cast<std::uint8_t>(child>>(i*8));}return result;}
struct Store;struct Session;struct Action;struct Response;struct Watch;struct Coordinator;
enum class CountKind{Session,Action,Response,Watch,Other};
struct Hold {std::shared_ptr<Store> store;Usage usage;CountKind kind=CountKind::Other;bool counted=false;~Hold();};
struct TargetStamp {ObjectId target;TargetInstanceId instance;std::uint64_t lifecycle;std::shared_ptr<PortLifetime> owner;};
struct TargetIdentity {ObjectId target;TargetInstanceId instance;};
struct Store {
 PolicyBudget budget;std::mutex mutex;bool closed=false;std::uint64_t serial=0,sequence=0,generation=1;
 Usage used;std::size_t live_sessions=0,live_actions=0,live_responses=0,live_watches=0,queued=0,bytes=0;
 std::shared_ptr<const PolicyConfiguration> config;std::vector<TargetIdentity> target_ids;
 std::shared_ptr<TrustedAuthenticationPort> auth;std::shared_ptr<ClockPort> clock;std::shared_ptr<TrustedGroupDigestPort> digest;std::shared_ptr<ExecutionAccessSourcePort> source;ObservationSourceIdentity source_id;
};
std::size_t&counter(Store&s,CountKind k){if(k==CountKind::Session)return s.live_sessions;if(k==CountKind::Action)return s.live_actions;if(k==CountKind::Response)return s.live_responses;return s.live_watches;}
std::size_t maximum(const Store&s,CountKind k){if(k==CountKind::Session)return s.budget.sessions;if(k==CountKind::Action)return s.budget.active_actions;if(k==CountKind::Response)return s.budget.active_responses;return s.budget.active_watches;}
Hold::~Hold(){if(counted){std::lock_guard lock(store->mutex);store->used.declarations-=usage.declarations;store->used.text-=usage.text;if(kind!=CountKind::Other)--counter(*store,kind);}}
Result<void> acquire(Hold&h){auto&s=*h.store;if(s.closed)return deny(PolicyErrc::StoreClosed);if((h.kind!=CountKind::Other&&counter(s,h.kind)>=maximum(s,h.kind))||h.usage.declarations>s.budget.declarations-s.used.declarations||h.usage.text>s.budget.text_bytes-s.used.text)return deny(PolicyErrc::BudgetExceeded);s.used.declarations+=h.usage.declarations;s.used.text+=h.usage.text;if(h.kind!=CountKind::Other)++counter(s,h.kind);h.counted=true;return {};}
Result<std::uint64_t> next(Store&s){if(s.sequence>=s.budget.identity_limit)return failure<std::uint64_t>(PolicyErrc::IdentityExhausted);return ++s.sequence;}
Result<void> bump(Store&s){if(s.generation>=s.budget.generation_limit)return deny(PolicyErrc::GenerationExhausted);++s.generation;return {};}
Result<void> source_current(const std::shared_ptr<Store>&s){const auto current=s->source->identity();std::lock_guard lock(s->mutex);if(current!=s->source_id)s->closed=true;return s->closed?deny(PolicyErrc::StoreClosed):Result<void>{};}
struct Session {
 Hold hold;ConnectionId id;PrincipalRef principal;PrincipalKind kind;std::optional<PrincipalRef> delegated_by;
 std::shared_ptr<const DelegationInput> scope,ceiling;TimePoint deadline;std::uint64_t generation=1;bool closed=false;
 CallerAuthorityPort* caller_port=nullptr;TargetAuthorityPort* target_port=nullptr;
 std::size_t watches=0;
};
struct SessionEntity {std::shared_ptr<Session> session;std::shared_ptr<CallerAuthorityPort> callers;std::shared_ptr<TargetAuthorityPort> targets;std::shared_ptr<ObservationAuthorization> observation;};
Result<void> current(const Session&s){if(s.hold.store->closed)return deny(PolicyErrc::StoreClosed);if(s.closed)return deny(PolicyErrc::SessionClosed);if(s.hold.store->clock->now()>=s.deadline)return deny(PolicyErrc::Expired);return {};}
bool contained_rule(const ScopeRule&a,const ScopeRule&b){return a.use==b.use&&(!b.operation||a.operation==b.operation)&&subset(a.permissions,b.permissions)&&subset(a.targets,b.targets)&&subset(a.owners,b.owners)&&subset(a.fields,b.fields);}
bool narrower(const DelegationInput&a,const DelegationInput&b){if(a.deadline>b.deadline||a.allow_redelegation)return false;for(auto&r:a.rules){bool found=false;for(auto&old:b.rules)if(contained_rule(r,old))found=true;if(!found)return false;}return true;}
const OperationPolicyInput* operation_policy(const Store&s,const OperationSelector&o){for(auto&x:s.config->operations)if(x.operation==o)return &x;return nullptr;}
const TargetPolicyInput* target_policy(const Store&s,ObjectId t){for(auto&x:s.config->targets)if(x.target==t)return &x;return nullptr;}
const UsePolicyInput* use_policy(const Store&s,AccessUse use){for(auto&x:s.config->uses)if(x.use==use)return &x;return nullptr;}
const PrincipalPolicyInput* principal_policy(const Store&s,PrincipalRef p){for(auto&x:s.config->principals)if(x.principal==p)return &x;return nullptr;}
bool match(const std::vector<ScopeRule>&rules,AccessUse use,const OperationSelector&o,ObjectId target,PrincipalRef owner,const std::vector<Name>&permissions,std::optional<SummaryField>field,bool preowner=false){
 for(auto&r:rules){if(r.use!=use||!subset(permissions,r.permissions))continue;if(preowner){if(has(r.owners,owner))return true;continue;}if(r.operation&&*r.operation!=o)continue;if(!has(r.targets,target))continue;if(use!=AccessUse::Invoke&&use!=AccessUse::Catalog&&!has(r.owners,owner))continue;if(field&&!has(r.fields,*field))continue;return true;}return false;
}
bool allowed(const Session&session,AccessUse use,const OperationSelector&o,ObjectId t,PrincipalRef owner,std::optional<SummaryField>field={}){
 auto&s=*session.hold.store;auto*p=principal_policy(s,session.principal);auto*op=operation_policy(s,o);auto*target=target_policy(s,t);if(!p||!op||!target)return false;const std::vector<Name>*permissions=&op->required_permissions;const UsePolicyInput*u=nullptr;if(use!=AccessUse::Invoke){u=use_policy(s,use);if(!u)return false;permissions=&u->required_permissions;}
 return match(p->rules,use,o,t,owner,*permissions,field)&&match(session.scope->rules,use,o,t,owner,*permissions,field)&&match(session.ceiling->rules,use,o,t,owner,*permissions,field)&&match(op->module_rules,use,o,t,owner,*permissions,field)&&match(target->rules,use,o,t,owner,*permissions,field)&&(!u||match(u->module_rules,use,o,t,owner,*permissions,field));
}
bool owner_candidate(const Session&session,AccessUse use,PrincipalRef owner){auto&s=*session.hold.store;auto*p=principal_policy(s,session.principal);auto*u=use_policy(s,use);if(!p||!u||s.config->operations.empty())return false;auto&o=s.config->operations[0].operation;ObjectId t{};return match(p->rules,use,o,t,owner,u->required_permissions,{},true)&&match(session.scope->rules,use,o,t,owner,u->required_permissions,{},true)&&match(session.ceiling->rules,use,o,t,owner,u->required_permissions,{},true)&&match(u->module_rules,use,o,t,owner,u->required_permissions,{},true);}
std::optional<TargetStamp> stamp(const Store&s,ObjectId t){auto*p=target_policy(s,t);if(!p)return {};for(auto&i:s.target_ids)if(i.target==t)return TargetStamp{t,i.instance,p->lifecycle_generation,p->lifetime_owner};return {};}
bool stamp_current(const Store&s,const TargetStamp&t){auto now=stamp(s,t.target);return now&&now->instance==t.instance&&now->lifecycle==t.lifecycle;}
class Grant final:public CallerGrant {
public:std::shared_ptr<Session> session;CallerAuthorityPort*issuer;std::uint64_t generation;CallerDescription description_;
 Grant(std::shared_ptr<Session>s,CallerAuthorityPort*i):session(std::move(s)),issuer(i),generation(0),description_{session->principal,session->delegated_by,{}}{}
 const CallerDescription&description()const noexcept override{return description_;}
};
class CallerPort final:public CallerAuthorityPort {
 std::shared_ptr<Session>s_;
public:explicit CallerPort(std::shared_ptr<Session>s):s_(std::move(s)){}
 Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription&d)override{
  auto grant=std::make_shared<Grant>(s_,this);std::lock_guard lock(s_->hold.store->mutex);auto c=current(*s_);if(!c)return make_unexpected(c.error());if(d.principal!=s_->principal||d.delegated_by!=s_->delegated_by||!d.tags.empty())return failure<std::shared_ptr<const CallerGrant>>(PolicyErrc::AuthenticationFailed);grant->generation=s_->generation;return std::shared_ptr<const CallerGrant>(std::move(grant));
 }
 Result<void> validate(const CallerGrant&g)const override{std::lock_guard lock(s_->hold.store->mutex);auto c=current(*s_);if(!c)return c;auto*p=dynamic_cast<const Grant*>(&g);if(!p||p->session!=s_||p->issuer!=this||p->generation!=s_->generation)return deny(PolicyErrc::InvalidAuthority);return {};}
};
class Target final:public TargetView {
public:std::shared_ptr<Session>session;TargetStamp value;std::uint64_t permission,delegation;TargetAuthorityPort*issuer;
 Target(std::shared_ptr<Session>s,TargetStamp t,TargetAuthorityPort*i):session(std::move(s)),value(std::move(t)),permission(0),delegation(0),issuer(i){}
 ObjectId target()const noexcept override{return value.target;}
};
class TargetPort final:public TargetAuthorityPort {
 std::shared_ptr<Session>s_;
public:explicit TargetPort(std::shared_ptr<Session>s):s_(std::move(s)){}
 Result<std::shared_ptr<const TargetView>> resolve(const CallerView&caller,ObjectId requested)override;
 Result<void> validate(const TargetView&v,const CallerView&caller,ObjectId expected)const override;
};
struct Verified {std::shared_ptr<Session>session;std::shared_ptr<const CallerGrant>grant;CallerView view;std::shared_ptr<CallerAuthorityPort>authority;};
struct Group {ActionRequest request;};
enum class ActionStatus{Prepared,Issued,Consumed,Cancelled};
struct Action {Hold hold;ActionId id;std::shared_ptr<Verified>caller;std::shared_ptr<const GroupSnapshot>group;ActionRequest request;ContractDigest digest;std::vector<TargetStamp>targets;PermitBinding binding;std::uint64_t delegation;ActionStatus status=ActionStatus::Prepared;std::shared_ptr<const ActionPermit>permit;};
class Permit final:public ActionPermit {
public:std::weak_ptr<Action>action;const PermitBinding value;
 Permit(std::shared_ptr<Action>a):action(a),value(a->binding){}
 const PermitBinding&binding()const noexcept override{return value;}
};
struct Entry {ExecutionAccessInput original;OperationSelector operation;std::vector<TargetStamp>targets;std::vector<SummaryField>fields;};
struct Projection {ProjectionKind kind;std::variant<std::shared_ptr<const ExecutionSummary>,ListPage,ChangeHint>value;};
enum class ResponseStatus{Fresh,Queued,Started,Dropped,Unknown};
struct Response {Hold hold;ResponseId id;std::shared_ptr<Session>session;AccessUse use;TimePoint deadline;std::shared_ptr<const ProjectionSnapshot>projection;std::vector<Entry>entries;ObservationSourceIdentity source;std::uint64_t permission,delegation;ResponseStatus status=ResponseStatus::Fresh;};
struct Watch {Hold hold;WatchKey key;std::uint64_t generation=1;std::shared_ptr<Session>session;ObservationFilter filter;TimePoint deadline;ObservationSourceIdentity source;std::vector<Entry>entries;bool closed=false;~Watch(){if(hold.counted){std::lock_guard lock(hold.store->mutex);--session->watches;}}};
struct Observation {std::shared_ptr<Session>session;};
struct Frame {std::shared_ptr<const ResponseAuthorization>response;std::shared_ptr<Watch>watch;std::shared_ptr<const ProjectionSnapshot>projection;std::vector<Entry>entries;std::unique_ptr<PreparedTransmission>transmission;std::unique_ptr<TransmissionReservation>reservation;TimePoint deadline;std::uint64_t permission,delegation;bool charged=false;};
struct Coordinator {std::shared_ptr<Session>session;std::shared_ptr<TransmissionStartPort>sink;std::shared_ptr<ProjectionEncoderPort>encoder;std::vector<std::shared_ptr<Frame>>queue;std::mutex pumping;~Coordinator();};
}
#define POLICY_STATE(N,R) struct N::State{std::shared_ptr<detail::R>value;}; N::N(std::shared_ptr<State>s):state_(std::move(s)){} N::~N()=default
POLICY_STATE(PolicyStore,Store);POLICY_STATE(PolicyAdministration,Store);POLICY_STATE(VerifiedCaller,Verified);POLICY_STATE(GroupSnapshot,Group);POLICY_STATE(ActionAuthorization,Action);POLICY_STATE(ObservationAuthorization,Observation);POLICY_STATE(WatchAuthorization,Watch);POLICY_STATE(ResponseAuthorization,Response);POLICY_STATE(ProjectionSnapshot,Projection);POLICY_STATE(SendCoordinator,Coordinator);
#undef POLICY_STATE
struct SessionAuthority::State{std::shared_ptr<detail::SessionEntity>value;};SessionAuthority::SessionAuthority(std::shared_ptr<State>s):state_(std::move(s)){}SessionAuthority::~SessionAuthority(){(void)close();}
namespace detail {
struct Access {
 template<class T>static auto record(const T&v){return v.state_->value;}
 template<class T,class R>static std::shared_ptr<T> make(std::shared_ptr<R>r){return std::shared_ptr<T>(new T(std::make_shared<typename T::State>(typename T::State{std::move(r)})));}
 template<class T,class R>static std::unique_ptr<T> make_unique(std::shared_ptr<R>r){return std::unique_ptr<T>(new T(std::make_shared<typename T::State>(typename T::State{std::move(r)})));}
 static PageBinding page(PageBindingData d){return PageBinding(std::move(d));}
 static std::unique_ptr<PreparedTransmission> transmission(std::vector<std::byte>b,TransmissionBinding t,std::shared_ptr<const ProjectionSnapshot>p){return std::unique_ptr<PreparedTransmission>(new PreparedTransmission(std::move(b),std::move(t),std::move(p)));}
};
}
using namespace detail;
const OperationSelector&GroupSnapshot::envelope()const noexcept{return state_->value->request.envelope;}
ObjectId GroupSnapshot::anchor_target()const noexcept{return state_->value->request.anchor_target;}
std::span<const MemberRequest>GroupSnapshot::members()const noexcept{return state_->value->request.members;}
const CallerView&VerifiedCaller::view()const noexcept{return state_->value->view;}
std::shared_ptr<CallerAuthorityPort>VerifiedCaller::authority()const noexcept{return state_->value->authority;}
std::shared_ptr<CallerAuthorityPort>SessionAuthority::callers()const noexcept{return state_->value->callers;}
std::shared_ptr<TargetAuthorityPort>SessionAuthority::targets()const noexcept{return state_->value->targets;}
std::shared_ptr<ObservationAuthorization>SessionAuthority::observations()const noexcept{return state_->value->observation;}
ProjectionKind ProjectionSnapshot::kind()const noexcept{return state_->value->kind;}
const std::variant<std::shared_ptr<const ExecutionSummary>,ListPage,ChangeHint>&ProjectionSnapshot::value()const noexcept{return state_->value->value;}
const ProjectionSnapshot&ResponseAuthorization::projection()const noexcept{return *state_->value->projection;}
AccessUse ResponseAuthorization::use()const noexcept{return state_->value->use;}
TimePoint ResponseAuthorization::deadline()const noexcept{return state_->value->deadline;}
WatchKey WatchAuthorization::key()const noexcept{return state_->value->key;}
std::uint64_t WatchAuthorization::generation()const noexcept{return state_->value->generation;}
const ObservationFilter&WatchAuthorization::filter()const noexcept{return state_->value->filter;}
TimePoint WatchAuthorization::deadline()const noexcept{return state_->value->deadline;}
}
