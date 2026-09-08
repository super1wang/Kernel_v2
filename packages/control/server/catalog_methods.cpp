#include <atomic>
#include <ock/control/catalog_methods.hpp>

namespace ock::control {
namespace {
struct State {
  std::shared_ptr<const catalog::Catalog> catalog;
  std::shared_ptr<const runtime::policy::SessionAuthority> session;
  foundation::ObjectId target;
  std::atomic<bool> closed{false};
};
class Port final : public MethodPort {
public:
  Port(std::shared_ptr<State> state, bool describe)
      : state_(std::move(state)), describe_(describe) {}
  void disconnect() noexcept override { state_->closed = true; }
  Result<data::Payload> call(const runtime::policy::VerifiedCaller &caller,
                             data::ValueView params) override {
    if (state_->closed)
      return foundation::make_unexpected(error(ProtocolErrc::Closed));
    if (describe_) {
      auto name = params.at("name").string(),
           version = params.at("version").string();
      if (!name || !version)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
      auto parsed_name = foundation::Name::parse(*name);
      auto parsed_version = contracts::OperationVersion::parse(*version, 128);
      if (!parsed_name || !parsed_version)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
      auto item =
          state_->catalog->describe(*state_->session, caller, state_->target,
                                    {*parsed_name, *parsed_version});
      if (!item)
        return unavailable();
      return catalog::Catalog::command_card(*item);
    }
    catalog::SearchRequest request;
    if (!params.at("prefix").missing()) {
      auto value = params.at("prefix").string();
      if (!value)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
      request.prefix = *value;
    }
    if (!params.at("offset").missing()) {
      auto value = params.at("offset").uint64();
      if (!value || *value > 4096)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
      request.offset = static_cast<std::size_t>(*value);
    }
    if (!params.at("page_size").missing()) {
      auto value = params.at("page_size").uint64();
      if (!value || !*value || *value > 200)
        return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
      request.page_size = static_cast<std::size_t>(*value);
    }
    auto page = state_->catalog->search(*state_->session, caller,
                                        state_->target, request);
    if (!page)
      return unavailable();
    data::PayloadBuilder b;
    (void)b.begin_object();
    (void)b.key("fingerprint");
    (void)b.string(page->fingerprint);
    (void)b.key("next_offset");
    if (page->next_offset)
      (void)b.uint64(*page->next_offset);
    else
      (void)b.null();
    (void)b.key("items");
    (void)b.begin_array();
    for (const auto &item : page->items) {
      const auto &definition = item.definition->description();
      (void)b.begin_object();
      (void)b.key("name");
      (void)b.string(definition.key.name.view());
      (void)b.key("version");
      (void)b.string(definition.key.version.text());
      (void)b.key("installed");
      (void)b.boolean(item.installed);
      (void)b.key("visible");
      (void)b.boolean(item.visible);
      (void)b.key("eligible");
      (void)b.boolean(item.eligible);
      (void)b.end_object();
    }
    (void)b.end_array();
    (void)b.end_object();
    return b.freeze();
  }

private:
  static Result<data::Payload> unavailable() {
    return foundation::make_unexpected(
        runtime::policy::policy_error(runtime::policy::PolicyErrc::Denied));
  }
  std::shared_ptr<State> state_;
  bool describe_;
};
} // namespace
Result<std::vector<Method>> catalog_methods(
    std::shared_ptr<const catalog::Catalog> catalog,
    std::shared_ptr<const runtime::policy::SessionAuthority> session,
    foundation::ObjectId target) {
  if (!catalog || !session || target.empty())
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  auto search = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"prefix":{"type":"string","maxLength":128},"offset":{"type":"integer","minimum":0,"maximum":4096},"page_size":{"type":"integer","minimum":1,"maximum":200}},"additionalProperties":false})");
  auto describe = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":96},"version":{"type":"string","minLength":1,"maxLength":128}},"required":["name","version"],"additionalProperties":false})");
  if (!search)
    return foundation::make_unexpected(search.error());
  if (!describe)
    return foundation::make_unexpected(describe.error());
  auto state = std::make_shared<State>();
  state->catalog = std::move(catalog);
  state->session = std::move(session);
  state->target = target;
  return std::vector<Method>{
      {"capabilities.search", *search, std::make_shared<Port>(state, false)},
      {"capabilities.describe", *describe,
       std::make_shared<Port>(state, true)}};
}
} // namespace ock::control
