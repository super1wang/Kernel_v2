#include <ock/control_client/client.hpp>
#include <algorithm>
#include <limits>
namespace ock::control_client {
foundation::Result<std::string> quote(std::string_view value) {
  data::PayloadBuilder builder;
  if(!builder.string(value)) return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  auto payload = builder.freeze();
  if(!payload) return foundation::make_unexpected(payload.error());
  return payload->encode();
}
foundation::Result<data::Payload> Client::exchange(std::string_view method,
    const data::Payload &params, std::stop_token stop) {
  if(sequence_ == (std::numeric_limits<std::uint64_t>::max)() || params.view().kind() != data::Kind::Object)
    return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  if(stop.stop_requested()) return foundation::make_unexpected(error(ClientErrc::Interrupted));
  auto encoded = params.encode(); auto name = quote(method);
  if(!encoded || !name) return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  const auto id = std::to_string(++sequence_);
  const auto wire = "{\"jsonrpc\":\"2.0\",\"id\":\""+id+"\",\"method\":"+*name+",\"params\":"+*encoded+"}";
  if(hello_.frame_bytes && wire.size()>hello_.frame_bytes)
    return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  auto request = data::Payload::parse(wire);
  if(!request) return foundation::make_unexpected(request.error());
  auto response = port_->exchange(*request,timeout_,stop);
  if(!response) return response;
  auto root = response->view();
  if(root.kind() != data::Kind::Object || root.size() != 3 ||
      root.at("jsonrpc").string() != "2.0" || root.at("id").string() != id ||
      root.at("result").missing() == root.at("error").missing())
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  if(!root.at("error").missing() &&
      (root.at("error").kind() != data::Kind::Object || !root.at("error").at("code").int64() ||
       !root.at("error").at("message").string()))
    return foundation::make_unexpected(error(ClientErrc::Protocol));
  return response;
}
foundation::Result<Client> Client::open(std::shared_ptr<ExchangePort> port,
    std::chrono::milliseconds timeout, std::stop_token stop) {
  if(!port || timeout.count() <= 0 || timeout > std::chrono::minutes(5))
    return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  Client client(std::move(port),timeout);
  auto parameters = data::Payload::parse(R"({"api_version":"ock.control/1"})");
  auto response = client.exchange("host.hello",*parameters,stop);
  if(!response) return foundation::make_unexpected(response.error());
  auto r = response->view().at("result");
  auto application = r.at("application_id").string(), instance = r.at("instance_id").string(),
       host = r.at("host_incarnation").string(), backend = r.at("observation_backend").string();
  auto size = r.at("budgets").at("frame_bytes").uint64();
  auto methods = r.at("supported_methods");
  auto identity = [](std::string_view s) {
    return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == s.npos && s.find_first_not_of('0') != s.npos;
  };
  if(r.at("api_version").string() != "ock.control/1" || !application || !foundation::Name::parse(*application) ||
      !instance || !identity(*instance) || !host || !identity(*host) || !backend ||
      (*backend != "absent" && *backend != "mock" && *backend != "managed") ||
      !size || *size < 1024 || *size > 4*1024*1024 || methods.kind() != data::Kind::Array || methods.size() > 33)
    return foundation::make_unexpected(error(ClientErrc::Incompatible));
  client.hello_ = {std::string(*application),std::string(*instance),std::string(*host),{}, {},std::string(*backend),static_cast<std::size_t>(*size)};
  auto epoch = r.at("dedup_epoch");
  if(epoch.kind() != data::Kind::Null) {
    auto value = epoch.string();
    if(!value || value->empty() || value->size() > 128) return foundation::make_unexpected(error(ClientErrc::Incompatible));
    client.hello_.dedup_epoch = std::string(*value);
  }
  for(std::size_t i = 0; i < methods.size(); ++i) {
    auto name = methods.at(i).string();
    if(!name || name->empty() || name->size() > 96 || client.supports(*name))
      return foundation::make_unexpected(error(ClientErrc::Incompatible));
    client.hello_.supported_methods.emplace_back(*name);
  }
  if(!client.supports("host.hello")) return foundation::make_unexpected(error(ClientErrc::Incompatible));
  return client;
}
bool Client::supports(std::string_view method) const noexcept {
  return std::find(hello_.supported_methods.begin(),hello_.supported_methods.end(),method) != hello_.supported_methods.end();
}
foundation::Result<data::Payload> Client::call(std::string_view method,const data::Payload &params,std::stop_token stop) {
  if(method == "host.hello" || !supports(method)) return foundation::make_unexpected(error(ClientErrc::MissingCapability));
  return exchange(method,params,stop);
}
int exit_code(data::ValueView response) noexcept {
  if(!response.at("error").missing()) {
    auto code = response.at("error").at("code").int64();
    return code && (*code == -32600 || *code == -32602) ? 2 : 3;
  }
  auto result = response.at("result");
  auto kind = result.at("kind").string();
  if(!kind) return 0; // capabilities/get/list 是查询对象，不是业务 Outcome。
  if(*kind == "Rejected") return 3;
  if(*kind == "Accepted") {
    auto id=result.at("execution_ref").at("execution_id").string();
    auto guarantee=result.at("acceptance_guarantee").string();
    return id&&id->size()==32&&id->find_first_not_of("0123456789abcdef")==id->npos&&
        id->find_first_not_of('0')!=id->npos&&(guarantee=="Volatile"||guarantee=="DurableAccepted")?0:6;
  }
  if(*kind != "Completed") return 6;
  auto outcome = result.at("outcome").at("kind").string();
  if(!outcome) return 6;
  if(*outcome == "ReadCompleted" || *outcome == "StateCommitted" || *outcome == "PlanCompleted") return 0;
  if(*outcome == "EffectResolved" || *outcome == "LifecycleResolved") {
    auto status = result.at("outcome").at("status").string();
    return status == "Succeeded" ? 0 : (status == "Failed" ? 4 : 6);
  }
  if(*outcome == "CancelledBeforeApply") return 9;
  if(*outcome == "Indeterminate") return 7;
  if(*outcome == "FailedBeforeApply" || *outcome == "PartialCompletion") return 4;
  return 6;
}
}
