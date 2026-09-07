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
Result<TimePoint> expires(TimePoint now,std::chrono::milliseconds ttl){auto count=ttl.count();using D=TimePoint::duration;if(count<=0||count>(std::numeric_limits<D::rep>::max)()/1000000)return failure<TimePoint>(PolicyErrc::InvalidInput);auto delta=std::chrono::duration_cast<D>(ttl);if(now.time_since_epoch().count()>(std::numeric_limits<D::rep>::max)()-delta.count())return failure<TimePoint>(PolicyErrc::InvalidInput);return now+delta;}
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
struct TargetIdentity {ObjectId target;TargetInstanceId instance;std::uint64_t lifecycle;};
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
struct Action {Hold hold;ActionId id;std::shared_ptr<Verified>caller;std::shared_ptr<const GroupSnapshot>group;ActionRequest request;ContractDigest digest;std::vector<TargetStamp>targets;PermitBinding binding;std::uint64_t delegation;ActionStatus status=ActionStatus::Prepared;std::shared_ptr<const ActionPermit>permit;explicit Action(ActionRequest r):request(std::move(r)),binding{{},request.envelope.operation,{},request.anchor_target,0,0,request.requested_deadline},delegation(0){};};
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

Result<PolicyAssembly> PolicyStore::create(PolicyBudget b,const PolicyConfiguration&input,std::shared_ptr<TrustedAuthenticationPort>auth,std::shared_ptr<ClockPort>clock,std::shared_ptr<TrustedGroupDigestPort>digest,std::shared_ptr<ExecutionAccessSourcePort>source){
 try{
  if(!valid_budget(b)||!owned(auth)||!owned(clock)||!owned(digest)||!owned(source))return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
  auto sid=source->identity();if(sid.host.empty()||sid.restore!=RestoreMode::Absent)return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
  bool good;auto usage=config_usage(input,b,good);if(!good)return failure<PolicyAssembly>(PolicyErrc::BudgetExceeded);
  for(auto ttl:{b.session_ttl,b.action_ttl,b.page_ttl,b.queued_ttl})if(!expires(clock->now(),ttl))return failure<PolicyAssembly>(PolicyErrc::InvalidInput);
  auto s=std::make_shared<Store>();s->budget=b;s->config=std::make_shared<const PolicyConfiguration>(input);s->used=usage;s->auth=std::move(auth);s->clock=std::move(clock);s->digest=std::move(digest);s->source=std::move(source);s->source_id=sid;
  auto serial=issue_process();if(!serial)return make_unexpected(serial.error());s->serial=*serial;s->target_ids.reserve(input.targets.size());
  for(auto&t:input.targets){auto n=next(*s);if(!n)return make_unexpected(n.error());s->target_ids.push_back({t.target,identity<TargetInstanceId>(s->serial,*n),t.lifecycle_generation});}
  return PolicyAssembly{Access::make<PolicyStore>(s),Access::make_unique<PolicyAdministration>(s)};
 }catch(const std::bad_alloc&){return failure<PolicyAssembly>(PolicyErrc::BudgetExceeded);}
}
Result<std::shared_ptr<SessionAuthority>> PolicyStore::open(const AuthenticationAttempt&attempt,const DelegationInput&requested){
 try{
  auto s=state_->value;auto check=source_current(s);if(!check)return make_unexpected(check.error());
  if(attempt.credential.empty()||attempt.credential.size()>s->budget.credential_bytes)return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::BudgetExceeded);
  Meter meter{s->budget};meter.rules(requested.rules);if(!meter.valid)return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::BudgetExceeded);
  auto scope=std::make_shared<const DelegationInput>(requested);AuthenticationAttempt credentials=attempt;auto authenticated=s->auth->authenticate(credentials);if(!authenticated)return make_unexpected(authenticated.error());
  auto&v=*authenticated;if(v.principal.principal_id.empty()||v.kind>PrincipalKind::Service||scope->allow_redelegation||v.ceiling.allow_redelegation)return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::UnsupportedDelegation);
  meter.rules(v.ceiling.rules);if(!meter.valid)return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::BudgetExceeded);
  auto ttl=expires(s->clock->now(),s->budget.session_ttl);if(!ttl)return make_unexpected(ttl.error());auto deadline=(std::min)({*ttl,scope->deadline,v.deadline,v.ceiling.deadline});
  auto session=std::make_shared<Session>();session->hold.store=s;session->hold.usage=meter.value;session->hold.kind=CountKind::Session;session->principal=v.principal;session->kind=v.kind;session->delegated_by=v.delegated_by;session->scope=std::move(scope);session->ceiling=std::make_shared<const DelegationInput>(v.ceiling);session->deadline=deadline;
  auto e=std::make_shared<SessionEntity>();e->session=session;e->callers=std::make_shared<CallerPort>(session);e->targets=std::make_shared<TargetPort>(session);e->observation=Access::make<ObservationAuthorization>(std::make_shared<Observation>(Observation{session}));session->caller_port=e->callers.get();session->target_port=e->targets.get();auto result=Access::make<SessionAuthority>(e);
  check=source_current(s);if(!check)return make_unexpected(check.error());
  {std::lock_guard lock(s->mutex);check=current(*session);if(!check)return make_unexpected(check.error());if(!principal_policy(*s,session->principal))return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::AuthenticationFailed);auto n=next(*s);if(!n)return make_unexpected(n.error());session->id=identity<ConnectionId>(s->serial,*n);check=acquire(session->hold);if(!check)return make_unexpected(check.error());}
  return result;
 }catch(const std::bad_alloc&){return failure<std::shared_ptr<SessionAuthority>>(PolicyErrc::BudgetExceeded);}
}
Result<void> PolicyAdministration::close_store(){auto s=state_->value;std::lock_guard lock(s->mutex);s->closed=true;return {};}
Result<void> SessionAuthority::close(){auto s=state_->value->session;std::lock_guard lock(s->hold.store->mutex);s->closed=true;return {};}
Result<std::shared_ptr<const VerifiedCaller>>SessionAuthority::verify(const CallerDescription&d){
 try{auto e=state_->value;auto g=e->callers->authenticate(d);if(!g)return make_unexpected(g.error());auto view=CallerView::check(e->callers,*g);if(!view)return make_unexpected(view.error());auto v=std::make_shared<Verified>(Verified{e->session,*g,*view,e->callers});return std::shared_ptr<const VerifiedCaller>(Access::make<VerifiedCaller>(v));}catch(const std::bad_alloc&){return failure<std::shared_ptr<const VerifiedCaller>>(PolicyErrc::BudgetExceeded);}
}
Result<void>SessionAuthority::restrict_delegation(const DelegationInput&input){
 try{auto session=state_->value->session;auto s=session->hold.store;Meter m{s->budget};m.rules(input.rules);if(!m.valid)return deny(PolicyErrc::BudgetExceeded);auto candidate=std::make_shared<const DelegationInput>(input);std::shared_ptr<const DelegationInput>old;
  {std::lock_guard lock(s->mutex);old=session->scope;}
  Meter previous{s->budget};previous.rules(old->rules);
  {std::lock_guard lock(s->mutex);auto c=current(*session);if(!c)return c;if(session->scope!=old)return deny(PolicyErrc::Busy);if(!narrower(*candidate,*old))return deny(PolicyErrc::UnsupportedDelegation);if(session->generation>=s->budget.generation_limit)return deny(PolicyErrc::GenerationExhausted);
   auto declarations=s->used.declarations-previous.value.declarations;auto text=s->used.text-previous.value.text;
   if(!charge(declarations,m.value.declarations,s->budget.declarations)||!charge(text,m.value.text,s->budget.text_bytes))return deny(PolicyErrc::BudgetExceeded);
   s->used={declarations,text};session->hold.usage.declarations=session->hold.usage.declarations-previous.value.declarations+m.value.declarations;session->hold.usage.text=session->hold.usage.text-previous.value.text+m.value.text;
   session->scope=std::move(candidate);++session->generation;session->deadline=(std::min)(session->deadline,session->scope->deadline);}return {};
 }catch(const std::bad_alloc&){return deny(PolicyErrc::BudgetExceeded);}
}
Result<std::shared_ptr<const TargetView>>TargetPort::resolve(const CallerView&caller,ObjectId requested){
 try{if(!caller.belongs_to(*s_->caller_port))return failure<std::shared_ptr<const TargetView>>(PolicyErrc::InvalidAuthority);auto cv=caller.revalidate();if(!cv)return make_unexpected(cv.error());auto store=s_->hold.store;std::optional<TargetStamp>captured;std::uint64_t generation,delegation;
  {std::lock_guard lock(store->mutex);auto c=current(*s_);if(!c)return make_unexpected(c.error());captured=stamp(*store,requested);if(!captured)return failure<std::shared_ptr<const TargetView>>(PolicyErrc::TargetUnavailable);generation=store->generation;delegation=s_->generation;}
  auto value=std::make_shared<Target>(s_,std::move(*captured),this);value->permission=generation;value->delegation=delegation;return std::shared_ptr<const TargetView>(value);
 }catch(const std::bad_alloc&){return failure<std::shared_ptr<const TargetView>>(PolicyErrc::BudgetExceeded);}
}
Result<void>TargetPort::validate(const TargetView&v,const CallerView&caller,ObjectId expected)const{
 if(!caller.belongs_to(*s_->caller_port))return deny(PolicyErrc::InvalidAuthority);auto cv=caller.revalidate();if(!cv)return cv;auto store=s_->hold.store;std::lock_guard lock(store->mutex);auto c=current(*s_);if(!c)return c;auto*t=dynamic_cast<const Target*>(&v);if(!t||t->session!=s_||t->issuer!=this||t->value.target!=expected||t->permission!=store->generation||t->delegation!=s_->generation||!stamp_current(*store,t->value))return deny(PolicyErrc::InvalidAuthority);return {};
}

namespace detail {
Result<void> action_current(const Action&a){
 auto&session=*a.caller->session;auto&s=*session.hold.store;auto c=current(session);if(!c)return c;
 if(a.status==ActionStatus::Cancelled)return deny(PolicyErrc::Cancelled);if(a.status==ActionStatus::Consumed)return deny(PolicyErrc::AlreadyConsumed);
 if(s.clock->now()>=a.binding.deadline)return deny(PolicyErrc::Expired);
 if(a.delegation!=session.generation||a.binding.permission_generation!=s.generation)return deny(PolicyErrc::Denied);
 auto*g=dynamic_cast<const Grant*>(a.caller->grant.get());if(!g||g->session!=a.caller->session||g->issuer!=session.caller_port||g->generation!=session.generation)return deny(PolicyErrc::InvalidAuthority);
 for(auto&t:a.targets)if(!stamp_current(s,t))return deny(PolicyErrc::TargetUnavailable);
 if(!allowed(session,AccessUse::Invoke,a.request.envelope,a.request.anchor_target,session.principal))return deny(PolicyErrc::Denied);
 for(auto&m:a.request.members)for(auto t:m.targets)if(!allowed(session,AccessUse::Invoke,m.operation,t,session.principal))return deny(PolicyErrc::Denied);
 return {};
}
Coordinator::~Coordinator(){if(session){auto s=session->hold.store;{std::lock_guard lock(s->mutex);for(auto&f:queue)if(f->charged){--s->queued;s->bytes-=f->transmission->bytes().size();f->charged=false;}}}}
}
Result<std::shared_ptr<ActionAuthorization>>SessionAuthority::prepare(const VerifiedCaller&caller,const ActionRequest&input){
 try{
  auto session=state_->value->session;auto s=session->hold.store;auto verified=Access::record(caller);if(verified->session!=session)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidAuthority);
  Meter meter{s->budget};meter.selector(input.envelope);meter.count(input.members.size());if(input.members.empty()||input.members.size()>s->budget.members||input.anchor_target.empty())return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidGroup);
  std::size_t target_count=1;for(auto&m:input.members){meter.selector(m.operation);meter.count(m.targets.size());if(m.targets.empty()||m.targets.size()>s->budget.targets||m.targets.size()>s->budget.declarations-target_count)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidGroup);target_count+=m.targets.size();for(auto t:m.targets)if(t.empty())return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidGroup);}
  meter.count(target_count);if(!meter.valid)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::BudgetExceeded);
  auto request=input;for(auto&m:request.members){std::sort(m.targets.begin(),m.targets.end(),[](auto&a,auto&b){return a.bytes<b.bytes;});m.targets.erase(std::unique(m.targets.begin(),m.targets.end()),m.targets.end());}
  auto group=Access::make<GroupSnapshot>(std::make_shared<Group>(Group{request}));auto fingerprint=s->digest->fingerprint(*group);if(!fingerprint)return make_unexpected(fingerprint.error());
  auto a=std::make_shared<Action>(std::move(request));a->hold.store=s;a->hold.kind=CountKind::Action;a->hold.usage=meter.value;a->caller=verified;a->group=group;a->digest=*fingerprint;a->targets.reserve(target_count);a->binding.group=*fingerprint;a->binding.principal=session->principal;auto ttl=expires(s->clock->now(),s->budget.action_ttl);if(!ttl)return make_unexpected(ttl.error());a->binding.deadline=(std::min)({input.requested_deadline,session->deadline,*ttl});auto result=Access::make<ActionAuthorization>(a);
  {std::lock_guard lock(s->mutex);auto c=current(*session);if(!c)return make_unexpected(c.error());auto*g=dynamic_cast<const Grant*>(verified->grant.get());if(!g||g->generation!=session->generation)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidAuthority);
   auto*op=operation_policy(*s,a->request.envelope);if(!op)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::PolicyNotInstalled);
   const bool single=a->request.members.size()==1&&a->request.members[0].operation==a->request.envelope;
   if((single&&!has(a->request.members[0].targets,a->request.anchor_target))||(!single&&!op->permits_group))return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::InvalidGroup);
   auto anchor=stamp(*s,a->request.anchor_target);if(!anchor)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::TargetUnavailable);a->targets.push_back(*anchor);a->binding.lifecycle_generation=anchor->lifecycle;a->binding.permission_generation=s->generation;a->delegation=session->generation;
   for(auto&m:a->request.members)for(auto t:m.targets){auto actual=stamp(*s,t);if(!actual)return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::TargetUnavailable);a->targets.push_back(*actual);}
   c=action_current(*a);if(!c)return make_unexpected(c.error());auto n=next(*s);if(!n)return make_unexpected(n.error());a->id=identity<ActionId>(s->serial,*n);c=acquire(a->hold);if(!c)return make_unexpected(c.error());
  }return result;
 }catch(const std::bad_alloc&){return failure<std::shared_ptr<ActionAuthorization>>(PolicyErrc::BudgetExceeded);}
}
Result<std::shared_ptr<const ActionPermit>>ActionAuthorization::issue(){return issue(*state_->value->caller->grant,state_->value->binding);}
Result<std::shared_ptr<const ActionPermit>>ActionAuthorization::issue(const CallerGrant&grant,const PermitBinding&binding){
 try{auto a=state_->value;auto candidate=std::make_shared<const Permit>(a);auto s=a->hold.store;std::lock_guard lock(s->mutex);auto c=action_current(*a);if(!c)return make_unexpected(c.error());if(&grant!=a->caller->grant.get()||binding!=a->binding)return failure<std::shared_ptr<const ActionPermit>>(PolicyErrc::InvalidPermit);if(!a->permit){a->permit=candidate;a->status=ActionStatus::Issued;}return a->permit;
 }catch(const std::bad_alloc&){return failure<std::shared_ptr<const ActionPermit>>(PolicyErrc::BudgetExceeded);}
}
Result<PermitBinding>ActionAuthorization::current_expected_binding()const{auto a=state_->value;auto candidate=a->binding;auto s=a->hold.store;std::lock_guard lock(s->mutex);auto c=action_current(*a);if(!c)return make_unexpected(c.error());return candidate;}
Result<void>ActionAuthorization::consume(const ActionPermit&permit,const PermitBinding&expected){auto a=state_->value;auto s=a->hold.store;std::lock_guard lock(s->mutex);auto c=action_current(*a);if(!c)return c;if(a->status!=ActionStatus::Issued||&permit!=a->permit.get()||expected!=a->binding)return deny(PolicyErrc::InvalidPermit);a->status=ActionStatus::Consumed;return {};}
Result<void>ActionAuthorization::cancel(){auto a=state_->value;auto s=a->hold.store;std::lock_guard lock(s->mutex);if(s->closed)return deny(PolicyErrc::StoreClosed);if(a->status==ActionStatus::Consumed)return deny(PolicyErrc::AlreadyConsumed);a->status=ActionStatus::Cancelled;return {};}

namespace detail {
template<class Change>Result<void> update_config(const std::shared_ptr<Store>&s,Change change,std::optional<ObjectId>replacement={}){
 try{
  std::shared_ptr<const PolicyConfiguration> old;std::vector<TargetIdentity> ids;ids.reserve(s->budget.targets);std::uint64_t epoch;
  {std::lock_guard lock(s->mutex);if(s->closed)return deny(PolicyErrc::StoreClosed);old=s->config;epoch=s->generation;for(auto&i:s->target_ids)ids.push_back(i);}
  auto candidate=std::make_shared<PolicyConfiguration>(*old);auto modified=change(*candidate);if(!modified)return modified;
  bool good;auto new_usage=config_usage(*candidate,s->budget,good);if(!good)return deny(PolicyErrc::BudgetExceeded);bool previous_good;auto previous=config_usage(*old,s->budget,previous_good);
  bool new_instance=false;std::size_t changed_index=0;
  if(replacement){auto t=std::find_if(candidate->targets.begin(),candidate->targets.end(),[&](auto&t){return t.target==*replacement;});if(t!=candidate->targets.end()){
   auto before=std::find_if(old->targets.begin(),old->targets.end(),[&](auto&t){return t.target==*replacement;});auto stamp=std::find_if(ids.begin(),ids.end(),[&](auto&i){return i.target==*replacement;});
   new_instance=before==old->targets.end()||before->lifetime_owner.get()!=t->lifetime_owner.get()||before->lifetime_owner.owner_before(t->lifetime_owner)||t->lifetime_owner.owner_before(before->lifetime_owner);
   if(stamp!=ids.end()){if(t->lifecycle_generation<stamp->lifecycle||(new_instance&&t->lifecycle_generation<=stamp->lifecycle))return deny(PolicyErrc::InvalidInput);stamp->lifecycle=t->lifecycle_generation;changed_index=static_cast<std::size_t>(stamp-ids.begin());}
   else{if(ids.size()>=s->budget.targets)return deny(PolicyErrc::BudgetExceeded);changed_index=ids.size();ids.push_back({t->target,{},t->lifecycle_generation});}
  }}
  {std::lock_guard lock(s->mutex);if(s->closed)return deny(PolicyErrc::StoreClosed);if(s->generation!=epoch||s->config!=old)return deny(PolicyErrc::Busy);if(s->generation>=s->budget.generation_limit)return deny(PolicyErrc::GenerationExhausted);
   auto declarations=s->used.declarations-previous.declarations;auto text=s->used.text-previous.text;if(!charge(declarations,new_usage.declarations,s->budget.declarations)||!charge(text,new_usage.text,s->budget.text_bytes))return deny(PolicyErrc::BudgetExceeded);
   if(new_instance){auto n=next(*s);if(!n)return make_unexpected(n.error());ids[changed_index].instance=identity<TargetInstanceId>(s->serial,*n);}
   s->config=std::move(candidate);s->target_ids.swap(ids);s->used={declarations,text};++s->generation;
  }return {};
 }catch(const std::bad_alloc&){return deny(PolicyErrc::BudgetExceeded);}
}
template<class T,class Key>Result<void> replace(std::vector<T>&v,const T&input,Key key){for(auto&x:v)if(key(x)==key(input)){x=input;return {};}v.push_back(input);return {};}
}
Result<void>PolicyAdministration::replace_principal_policy(const PrincipalPolicyInput&v){return update_config(state_->value,[&](auto&c){return replace(c.principals,v,[](auto&x){return x.principal;});});}
Result<void>PolicyAdministration::replace_operation_policy(const OperationPolicyInput&v){return update_config(state_->value,[&](auto&c){return replace(c.operations,v,[](auto&x){return x.operation.operation;});});}
Result<void>PolicyAdministration::replace_use_policy(const UsePolicyInput&v){return update_config(state_->value,[&](auto&c){return replace(c.uses,v,[](auto&x){return x.use;});});}
Result<void>PolicyAdministration::replace_target_policy(const TargetPolicyInput&v){return update_config(state_->value,[&](auto&c){return replace(c.targets,v,[](auto&x){return x.target;});},v.target);}
Result<void>PolicyAdministration::retire_target(ObjectId t){return update_config(state_->value,[&](auto&c)->Result<void>{auto found=std::find_if(c.targets.begin(),c.targets.end(),[&](auto&x){return x.target==t;});if(found==c.targets.end())return deny(PolicyErrc::TargetUnavailable);c.targets.erase(found);return {};});}
Result<void>PolicyAdministration::set_lifecycle(ObjectId t,std::uint64_t value){return update_config(state_->value,[&](auto&c)->Result<void>{for(auto&x:c.targets)if(x.target==t){if(value<=x.lifecycle_generation)return deny(PolicyErrc::InvalidInput);x.lifecycle_generation=value;return {};}return deny(PolicyErrc::TargetUnavailable);},t);}
}
