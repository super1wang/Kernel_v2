#include <algorithm>
#include <charconv>
#include <ock/control/router.hpp>
namespace ock::control {
namespace {
std::string quote(std::string_view input) {
  std::string output = "\"";
  for (unsigned char c : input) {
    if (c == '"' || c == '\\') {
      output += '\\';
      output += static_cast<char>(c);
    } else if (c < 32) {
      output += "\\u00";
      output += "0123456789abcdef"[c >> 4];
      output += "0123456789abcdef"[c & 15];
    } else
      output += static_cast<char>(c);
  }
  return output + '"';
}
RpcResponse fault(std::optional<std::string_view> id, int code,
                  std::string_view message, bool close = false) {
  return {"{\"jsonrpc\":\"2.0\",\"id\":" + (id ? quote(*id) : "null") +
              ",\"error\":{\"code\":" + std::to_string(code) +
              ",\"message\":" + quote(message) + "}}",
          close};
}
RpcResponse result(std::string_view id, std::string_view value) {
  return {"{\"jsonrpc\":\"2.0\",\"id\":" + quote(id) +
              ",\"result\":" + std::string(value) + "}",
          false};
}
bool stable_method(std::string_view method) {
  constexpr std::string_view methods[] = {"operation.invoke",
                                          "operation.submit",
                                          "capabilities.search",
                                          "capabilities.describe",
                                          "execution.get",
                                          "execution.wait",
                                          "execution.cancel",
                                          "execution.list",
                                          "notifications.subscribe",
                                          "notifications.unsubscribe",
                                          "intent.resolve",
                                          "result.read",
                                          "logs.read",
                                          "logs.follow",
                                          "health.inspect"};
  return std::find(std::begin(methods), std::end(methods), method) !=
         std::end(methods);
}
} // namespace
Router::Router(Router &&other) noexcept
    : hello_(std::move(other.hello_)), caller_(std::move(other.caller_)),
      methods_(std::move(other.methods_)), greeted_(other.greeted_),
      closed_(other.closed_) { other.closed_ = true; }
Router &Router::operator=(Router &&other) noexcept {
  if (this != &other) {
    close();
    hello_ = std::move(other.hello_);
    caller_ = std::move(other.caller_);
    methods_ = std::move(other.methods_);
    greeted_ = other.greeted_;
    closed_ = other.closed_;
    other.closed_ = true;
  }
  return *this;
}
Router::~Router() { close(); }
void Router::close() noexcept {
  if (closed_)
    return;
  closed_ = true;
  for (auto &method : methods_)
    method.port->disconnect();
}
Result<Router>
Router::create(Hello hello,
               std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
               std::vector<Method> methods) {
  auto identity = [](std::string_view text) {
    return text.size() == 32 &&
           text.find_first_not_of("0123456789abcdef") == text.npos &&
           text.find_first_not_of('0') != text.npos;
  };
  if (!foundation::Name::parse(hello.application_id) ||
      !identity(hello.instance_id) || !identity(hello.host_incarnation) ||
      (hello.store_id && !identity(*hello.store_id)))
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  if (hello.restore_generation) {
    auto &text = *hello.restore_generation;
    std::uint64_t value = 0;
    auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || text.front() == '0' || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size())
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  }
  if (!caller || !caller->view().revalidate() || methods.size() > 32 ||
      hello.application_id.empty() || hello.instance_id.empty() ||
      hello.host_incarnation.empty() || hello.application_id.size() > 128 ||
      hello.instance_id.size() > 128 || hello.host_incarnation.size() > 128 ||
      hello.store_id.has_value() != hello.restore_generation.has_value() ||
      hello.frame_bytes < 1024 || hello.frame_bytes > 4 * 1024 * 1024)
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  for (std::size_t i = 0; i < methods.size(); ++i) {
    if (!stable_method(methods[i].name) || !methods[i].port)
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
    for (std::size_t j = 0; j < i; ++j)
      if (methods[j].name == methods[i].name)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  }
  auto has = [&](std::string_view name) {
    return std::any_of(methods.begin(), methods.end(),
                       [&](const auto &m) { return m.name == name; });
  };
  const bool subscriptions =
      has("notifications.subscribe") || has("notifications.unsubscribe");
  const bool observes = subscriptions || has("execution.list") || has("execution.get");
  if ((hello.observation_backend != "absent" &&
       hello.observation_backend != "mock" &&
       hello.observation_backend != "managed") ||
      (subscriptions &&
       (!has("notifications.subscribe") || !has("notifications.unsubscribe") ||
        hello.observation_backend == "absent")) ||
      (observes && hello.observation_backend == "absent") ||
      (!observes && hello.observation_backend != "absent"))
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  return Router(std::move(hello), std::move(caller), std::move(methods));
}
Result<RpcResponse> Router::dispatch(data::ValueView input) {
  if (closed_)
    return foundation::make_unexpected(error(ProtocolErrc::Closed));
  auto parsed = request(input);
  if (!parsed) {
    close();
    if (input.at("id").missing())
      return RpcResponse{{}, true};
    return fault({}, -32600, "Invalid Request", true);
  }
  const auto &r = *parsed;
  if (!caller_->view().revalidate()) {
    close();
    return fault(r.id, -32001, "Session closed", true);
  }
  if (!greeted_) {
    if (r.method != "host.hello" || r.params.kind() != data::Kind::Object ||
        r.params.size() != 1 ||
        r.params.at("api_version").string() != "ock.control/1") {
      close();
      return fault(r.id, -32002, "Compatible host.hello required", true);
    }
    data::PayloadBuilder b;
    auto text = [&](std::string_view key, std::string_view value) {
      (void)b.key(key);
      (void)b.string(value);
    };
    (void)b.begin_object();
    text("application_id", hello_.application_id);
    text("instance_id", hello_.instance_id);
    text("host_incarnation", hello_.host_incarnation);
    text("api_version", "ock.control/1");
    (void)b.key("plan_version");
    (void)b.null();
    (void)b.key("dedup_epoch");
    (void)b.null();
    for (const auto &[key, value] :
         {std::pair{"store_id", hello_.store_id},
          std::pair{"restore_generation", hello_.restore_generation}}) {
      (void)b.key(key);
      if (value)
        (void)b.string(*value);
      else
        (void)b.null();
    }
    (void)b.key("supported_methods");
    (void)b.begin_array();
    (void)b.string("host.hello");
    bool observes = false;
    for (const auto &method : methods_) {
      (void)b.string(method.name);
      if (method.name == "notifications.subscribe")
        observes = true;
    }
    (void)b.end_array();
    (void)b.key("installed_capabilities");
    (void)b.begin_array();
    std::vector<std::string_view> capabilities;
    for (const auto &method : methods_) {
      std::string_view group(method.name.data(), method.name.find('.'));
      if (std::find(capabilities.begin(), capabilities.end(), group) ==
          capabilities.end()) {
        capabilities.push_back(group);
        (void)b.string(group);
      }
    }
    (void)b.end_array();
    (void)b.key("notification_protocol");
    if (observes)
      (void)b.string("ock.notifications/1");
    else
      (void)b.null();
    text("observation_backend", hello_.observation_backend);
    (void)b.key("budgets");
    (void)b.begin_object();
    (void)b.key("frame_bytes");
    (void)b.uint64(hello_.frame_bytes);
    (void)b.end_object();
    (void)b.end_object();
    auto value = b.freeze();
    if (!value)
      return foundation::make_unexpected(value.error());
    auto json = value->encode();
    if (!json)
      return foundation::make_unexpected(json.error());
    auto response = result(r.id, *json);
    if (response.json.size() > hello_.frame_bytes) {
      close();
      return foundation::make_unexpected(error(ProtocolErrc::BudgetExceeded));
    }
    greeted_ = true;
    return response;
  }
  for (auto &method : methods_)
    if (method.name == r.method) {
      if (!method.parameters.validate(r.params))
        return fault(r.id, -32602, "Invalid params");
      // 无结构化 Outcome 的异常可能位于业务边界之后；关闭连接，不能伪造
      // Rejected。
      try {
        auto value = method.port->dispatch(*caller_, r.id, r.params);
        if (!value) {
          const auto code = value.error().code();
          if (r.method.starts_with("notifications.") ||
              r.method == "execution.list" || r.method == "execution.get") {
            if (code.domain().name() == "ock.control.cursor")
              return code.value() == 2 ? fault(r.id, -32012, "CursorExpired")
                                       : fault(r.id, -32011, "CursorInvalid");
            if (code == runtime::policy::policy_error(runtime::policy::PolicyErrc::CursorInvalid).code())
              return fault(r.id, -32011, "CursorInvalid");
            if (code == error(ProtocolErrc::BudgetExceeded).code() ||
                code == runtime::policy::policy_error(runtime::policy::PolicyErrc::BudgetExceeded).code())
              return fault(r.id, -32013, "BudgetExceeded");
            if (code == error(ProtocolErrc::InvalidRequest).code())
              return fault(r.id, -32602, "Invalid params");
            // 隐藏对象与无权对象使用同一公开结论，内部政策码不得泄露存在性。
            return fault(r.id, -32010, "NotAvailable");
          }
          return result(r.id,
                        "{\"status\":\"Rejected\",\"reason\":{\"domain\":" +
                            quote(code.domain().name()) +
                            ",\"code\":" + std::to_string(code.value()) + "}}");
        }
        if (!*value)
          return RpcResponse{{}, false, true};
        auto json = (*value)->encode();
        if (!json) {
          close();
          return foundation::make_unexpected(json.error());
        }
        auto response = result(r.id, *json);
        if (response.json.size() > hello_.frame_bytes) {
          close();
          return foundation::make_unexpected(
              error(ProtocolErrc::BudgetExceeded));
        }
        return response;
      } catch (...) {
        close();
        return RpcResponse{{}, true};
      }
    }
  return fault(r.id, -32601, "Method not found");
}
} // namespace ock::control
