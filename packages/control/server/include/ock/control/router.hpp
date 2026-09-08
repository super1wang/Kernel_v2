#pragma once
#include <ock/dynamic/binding/schema.hpp>
#include <ock/control_protocol/protocol.hpp>
#include <ock/runtime/policy.hpp>
namespace ock::control {
// 可信装配端口；operation 方法必须适配同一治理调用，禁止装配裸业务 Handler。
class MethodPort : public contracts::PortLifetime {
public:
  virtual Result<data::Payload> call(const runtime::policy::VerifiedCaller &,
                                     data::ValueView) {
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  }
  // nullopt 仅表示完整响应已进入连接发送序列；Router 不得生成第二份响应。
  virtual Result<std::optional<data::Payload>>
  dispatch(const runtime::policy::VerifiedCaller &caller, std::string_view,
           data::ValueView params) {
    auto value = call(caller, params);
    if (!value)
      return foundation::make_unexpected(value.error());
    return std::optional<data::Payload>(std::move(*value));
  }
  virtual void disconnect() noexcept {}
};
struct Method {
  std::string name;
  binding::CompiledSchema parameters;
  std::shared_ptr<MethodPort> port;
};
struct Hello {
  std::string application_id, instance_id, host_incarnation;
  std::optional<std::string> store_id, restore_generation;
  std::string observation_backend = "absent";
  std::size_t frame_bytes = 4 * 1024 * 1024;
};
struct RpcResponse {
  std::string json;
  bool close = false;
  bool queued = false;
};
class Router {
public:
  Router(const Router &) = delete;
  Router &operator=(const Router &) = delete;
  Router(Router &&) noexcept;
  Router &operator=(Router &&) noexcept;
  ~Router();
  void close() noexcept;
  static Result<Router>
      create(Hello, std::shared_ptr<const runtime::policy::VerifiedCaller>,
             std::vector<Method>);
  Result<RpcResponse> dispatch(data::ValueView);

private:
  Router(Hello hello,
         std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
         std::vector<Method> methods)
      : hello_(std::move(hello)), caller_(std::move(caller)),
        methods_(std::move(methods)) {}
  Hello hello_;
  std::shared_ptr<const runtime::policy::VerifiedCaller> caller_;
  std::vector<Method> methods_;
  bool greeted_ = false, closed_ = false;
};
} // namespace ock::control
