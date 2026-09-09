#include <ock/control_client/watch.hpp>
#include <ock/control_protocol/observation_wire.hpp>
namespace ock::control_client {
using foundation::Result;
Result<std::unique_ptr<Watch>> Watch::open(Client &client,NotificationPort &port,std::string execution,std::stop_token stop) {
  if(client.hello().observation_backend!="managed" || !client.supports("notifications.subscribe") ||
      !client.supports("notifications.unsubscribe") || !client.supports("execution.get"))
    return foundation::make_unexpected(error(ClientErrc::MissingCapability));
  auto quoted=quote(execution);
  if(!quoted) return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  auto identity=data::Payload::parse(*quoted);
  if(!identity || !control::wire_id<foundation::TaskId>(identity->view())) return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  auto watch=std::unique_ptr<Watch>(new Watch(client,port,std::move(execution)));
  auto params=data::Payload::parse("{\"filter\":{\"executions\":[{\"execution_id\":\""+watch->execution_+"\"}]},\"topics\":[\"execution.progress\",\"execution.phase\",\"execution.fact\"]}");
  auto response=client.call("notifications.subscribe",*params,stop);
  if(!response) { port.close_notifications(); return foundation::make_unexpected(response.error()); }
  auto result=response->view().at("result");
  auto subscription=control::wire_id<control::SubscriptionId>(result.at("subscription_id"));
  auto stream=control::wire_id<control::StreamGeneration>(result.at("stream_generation"));
  if(!subscription || !stream || result.at("host_incarnation").string()!=client.hello().host_incarnation ||
      result.at("first_sequence").string()!="1" || result.at("replay_supported").boolean()!=false)
    { port.close_notifications(); return foundation::make_unexpected(error(ClientErrc::Protocol)); }
  watch->subscription_=control::wire_text(*subscription); watch->stream_=control::wire_text(*stream);
  // 先订阅，再 get。期间的通知由有界 NotificationPort 保留。
  auto snapshot=watch->refresh(stop);
  if(!snapshot) return foundation::make_unexpected(snapshot.error());
  return watch;
}
Watch::~Watch() { close(); }
Result<data::Payload> Watch::refresh(std::stop_token stop) {
  auto params=data::Payload::parse("{\"execution_ref\":{\"execution_id\":\""+execution_+"\"}}");
  auto response=client_.call("execution.get",*params,stop);
  if(!response) return foundation::make_unexpected(response.error());
  auto result=response->view().at("result");
  auto version=control::wire_count(result.at("observation_version"));
  auto phase=result.at("phase").string();
  if(!phase || (*phase!="Queued" && *phase!="WaitingResources" && *phase!="Running" &&
      *phase!="WaitingChild" && *phase!="Finalizing" && *phase!="Suspended" && *phase!="Terminal") ||
      (terminal_seen_ && *phase!="Terminal"))
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  if(!version || !*version || *version<version_ || result.at("host_incarnation").string()!=client_.hello().host_incarnation ||
      result.at("execution_ref").at("execution_id").string()!=execution_)
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  auto saved=response->clone(); if(!saved) return foundation::make_unexpected(saved.error());
  snapshot_.emplace(std::move(*saved));
  version_=*version;
  terminal_seen_=*phase=="Terminal";
  return response;
}
Result<data::Payload> Watch::snapshot() const {
  if(!snapshot_) return foundation::make_unexpected(error(ClientErrc::Protocol));
  return snapshot_->clone();
}
Result<std::optional<data::Payload>> Watch::next(std::chrono::milliseconds timeout,std::stop_token stop) {
  if(closed_) return foundation::make_unexpected(error(ClientErrc::Protocol));
  auto notification=port_.next_notification(timeout,stop);
  if(!notification) return foundation::make_unexpected(notification.error());
  auto resync=[&]() -> Result<std::optional<data::Payload>> {
    auto snapshot=refresh(stop); if(!snapshot) return foundation::make_unexpected(snapshot.error());
    return std::optional<data::Payload>(std::move(*snapshot));
  };
  if(!notification->frame) {
    return resync(); // 周期性准确读取；不依赖最后一条提示必达。
  }
  auto root=notification->frame->view(); auto params=root.at("params");
  if(root.at("jsonrpc").string()!="2.0" || root.at("method").string()!="notifications.event" || !root.at("id").missing())
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  if(params.at("subscription_id").string()!=subscription_ || params.at("stream_generation").string()!=stream_)
    return notification->local_gap ? resync() : Result<std::optional<data::Payload>>(std::optional<data::Payload>{}); // 已退订的旧世代不得污染当前观察。
  auto sequence=control::wire_count(params.at("sequence")),version=control::wire_count(params.at("observation_version"));
  auto gap=params.at("gap").boolean(); auto topic=params.at("topic").string();
  if(!sequence || !*sequence || !version || !*version || !gap || !topic ||
      (*topic!="execution.progress" && *topic!="execution.phase" && *topic!="execution.fact") ||
      params.at("host_incarnation").string()!=client_.hello().host_incarnation ||
      params.at("execution_ref").at("execution_id").string()!=execution_)
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  if(*sequence<=sequence_) return notification->local_gap ? resync() : Result<std::optional<data::Payload>>(std::optional<data::Payload>{});
  const bool missed=*gap || notification->local_gap || *sequence-sequence_!=1;
  sequence_=*sequence;
  if(missed || *topic!="execution.progress") return resync();
  if(terminal_seen_) return std::optional<data::Payload>{};
  if(*version<=version_) return std::optional<data::Payload>{};
  version_=*version;
  return std::optional<data::Payload>(std::move(*notification->frame));
}
void Watch::close() noexcept {
  if(closed_) return;
  closed_=true;
  if(subscription_.empty()) { port_.close_notifications(); return; }
  try {
    auto params=data::Payload::parse("{\"subscription_id\":\""+subscription_+"\",\"stream_generation\":\""+stream_+"\"}");
    if(params) (void)client_.call("notifications.unsubscribe",*params);
  } catch(...) {}
  port_.close_notifications();
}
}
