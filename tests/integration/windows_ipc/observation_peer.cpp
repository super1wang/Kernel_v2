#include "tests/contract/authorization/fixtures.hpp"
#include <ock/control/observation_transport.hpp>
#include <ock/local_ipc/pipe.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
using namespace ock;
// N1 mock source，只验证真实 Policy→Named Pipe 帧，不宣称真实 Task provider。
struct Events final : contracts::ObservationPort {
  std::shared_ptr<const contracts::ExecutionSummary> summary;
  unsigned leases = 0;
  struct Lease final : contracts::ObservationLease {
    Events &owner;
    explicit Lease(Events &value) : owner(value) { ++owner.leases; }
    ~Lease() { --owner.leases; }
  };
  foundation::Result<std::shared_ptr<const contracts::ExecutionSummary>> get_summary(const contracts::CallerView &,contracts::ExecutionRef) override { return summary; }
  foundation::Result<contracts::ListPage> list_summaries(const contracts::CallerView &,const contracts::ListRequest &) override {
    return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
  }
  foundation::Result<std::unique_ptr<contracts::ObservationLease>> observe_changes(const contracts::CallerView &,
      const contracts::ObservationFilter &,std::shared_ptr<contracts::ObservationReceiver> receiver) override {
    receiver->changed({summary,contracts::ObservationTopic::Progress,false});
    return std::unique_ptr<contracts::ObservationLease>(new Lease(*this));
  }
};
struct Context {
  policy_test::Env policy;
  Context() : policy([] { runtime::policy::PolicyBudget limits; limits.watches_per_principal=32; return limits; }()) {}
  std::shared_ptr<Events> events=std::make_shared<Events>();
  std::unique_ptr<control::SubscriptionConnection> subscription;
  std::optional<control::UnsubscribeRequest> watch;
};
int main(int argc,char **argv) try {
  CHECK(argc==3);
  std::string mode=argv[1];
  auto sid=local_ipc::current_user_sid(); CHECK(sid);
  local_ipc::PipeOptions options; options.instance=argv[2]; options.allowed_client_sids={*sid};
  std::mutex mutex; std::condition_variable changed;
  std::shared_ptr<Context> context;
  std::shared_ptr<local_ipc::Connection> pipe;
  bool ready=false,closed=false;
  auto server=local_ipc::Server::listen(options,[&](auto connection) {
    { std::lock_guard lock(mutex); pipe=connection; }
    std::weak_ptr<local_ipc::Connection> weak=connection;
    connection->start([&,weak](data::Payload) {
      try {
      auto c=weak.lock(); CHECK(c && c->peer() && c->peer()->sid()==*sid);
      auto value=std::make_shared<Context>();
      value->events->summary=value->policy.source->rows[0].second.summary;
      auto transport=control::ProtocolObservationTransport::create(c); CHECK(transport);
      auto subscription=control::SubscriptionConnection::create(value->policy.session,value->policy.caller,
          value->events,*transport,value->policy.clock,value->policy.source->source_id.host); CHECK(subscription);
      value->subscription=std::move(*subscription);
      control::SubscribeRequest request{{{{id<foundation::TaskId>()}}, {}, {contracts::ObservationTopic::Progress}},std::chrono::milliseconds(100)};
      auto watch=value->subscription->subscribe("subscribe",request); CHECK(watch);
      value->watch=*watch;
      { std::lock_guard lock(mutex); context=std::move(value); ready=true; changed.notify_all(); }
      } catch(const std::exception &e) { std::cerr << "Observation assembly: " << e.what() << std::endl; throw; }
    },[&] { std::lock_guard lock(mutex); closed=true; changed.notify_all(); });
  }); CHECK(server);
  std::cout << "READY" << std::endl;
  { std::unique_lock lock(mutex); CHECK(changed.wait_for(lock,std::chrono::seconds(10),[&]{return ready;})); }
  if(mode=="revoked") {
    CHECK(context->policy.assembly.administration->replace_principal_policy({policy_test::principal(),{}}));
    auto pumped=context->subscription->pump();
    CHECK(!pumped && pumped.error().code()==runtime::policy::policy_error(runtime::policy::PolicyErrc::Denied).code());
  } else {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool started=false;
    while(std::chrono::steady_clock::now()<deadline) {
      auto pumped=context->subscription->pump(); CHECK(pumped);
      if(*pumped==runtime::policy::StartResult::Started) { started=true; break; }
      CHECK(*pumped==runtime::policy::StartResult::NotStarted);
      std::this_thread::yield();
    }
    CHECK(started);
    CHECK(context->subscription->unsubscribe(*context->watch)==true);
    CHECK(context->subscription->pump()==runtime::policy::StartResult::NotStarted);
  }
  auto done=data::Payload::parse(R"({"done":true})"); auto frame=control::encode_frame(*done); CHECK(frame && pipe->send(*frame));
  { std::unique_lock lock(mutex); CHECK(changed.wait_for(lock,std::chrono::seconds(10),[&]{return closed;})); }
  context->subscription->close();
  CHECK(context->events->leases==0);
  (*server)->close(); CHECK(pipe->wait_closed(std::chrono::seconds(5)));
  std::cout << "Policy ordering and zero leases passed" << std::endl;
  return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
