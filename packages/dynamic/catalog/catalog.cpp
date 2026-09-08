#include <ock/dynamic/binding/record.hpp>
#include <ock/dynamic/catalog/catalog.hpp>
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
namespace ock::catalog {
namespace {
template <class T> Result<T> rejected() {
  return foundation::make_unexpected(binding::reject().error());
}
Result<std::string> hash(std::string_view text) {
  BCRYPT_ALG_HANDLE algorithm{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0)
    return rejected<std::string>();
  std::array<unsigned char, 32> digest{};
  auto status =
      BCryptHash(algorithm, nullptr, 0,
                 reinterpret_cast<PUCHAR>(const_cast<char *>(text.data())),
                 static_cast<ULONG>(text.size()), digest.data(), 32);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status < 0)
    return rejected<std::string>();
  std::string result;
  for (auto b : digest) {
    result += "0123456789abcdef"[b >> 4];
    result += "0123456789abcdef"[b & 15];
  }
  return result;
}
} // namespace
Result<Catalog>
Catalog::create(std::shared_ptr<const contracts::BindingPort> registry,
                std::vector<TypeSchema> schemas) {
  if (!registry || registry->size() > 4096 || schemas.size() > 8192)
    return rejected<Catalog>();
  for (std::size_t i = 0; i < schemas.size(); ++i) {
    if (schemas[i].schema.view().kind() != data::Kind::Object)
      return rejected<Catalog>();
    for (std::size_t j = 0; j < i; ++j)
      if (schemas[j].identity == schemas[i].identity)
        return rejected<Catalog>();
  }
  Catalog result(std::move(registry), std::move(schemas));
  for (std::size_t i = 0; i < result.registry_->size(); ++i) {
    auto entry = result.entry(static_cast<std::uint32_t>(i));
    if (!entry)
      return rejected<Catalog>();
    const auto &d = entry->definition->description();
    std::string content(d.key.name.view());
    content += '\0';
    content.append(d.key.version.text());
    content += '\0';
    content += d.docs;
    for (auto b : d.contract_digest.bytes)
      content += static_cast<char>(std::to_integer<unsigned char>(b));
    auto digest = hash(content);
    if (!digest)
      return rejected<Catalog>();
    result.entries_.push_back(std::move(*entry));
    result.fingerprints_.push_back(std::move(*digest));
  }
  return result;
}
Result<Description> Catalog::entry(std::uint32_t index) const {
  if (index < entries_.size())
    return entries_[index];
  auto definition = registry_->describe(index);
  if (!definition)
    return foundation::make_unexpected(definition.error());
  const TypeSchema *args = nullptr, *result = nullptr;
  for (auto &schema : schemas_) {
    if (schema.identity == (*definition)->args_contract())
      args = &schema;
    if (schema.identity == (*definition)->result_contract())
      result = &schema;
  }
  if (!args || !result)
    return rejected<Description>();
  return Description{*definition, args->schema, result->schema};
}
Result<Description>
Catalog::describe(const runtime::policy::SessionAuthority &session,
                  const runtime::policy::VerifiedCaller &caller,
                  foundation::ObjectId target,
                  const contracts::OperationKey &key) const {
  // 未安装和无权统一失败；不把存在性泄露给调用者。
  for (std::size_t i = 0; i < registry_->size(); ++i) {
    auto value = entry(static_cast<std::uint32_t>(i));
    if (!value)
      return value;
    const auto &d = value->definition->description();
    if (d.key != key)
      continue;
    auto status =
        session.inspect_catalog(caller, {d.key, d.contract_digest}, target);
    if (!status || !status->visible)
      return rejected<Description>();
    value->eligible = status->eligible;
    return value;
  }
  return rejected<Description>();
}
Result<Page> Catalog::search(const runtime::policy::SessionAuthority &session,
                             const runtime::policy::VerifiedCaller &caller,
                             foundation::ObjectId target,
                             const SearchRequest &request) const {
  if (request.prefix.size() > 128 || !request.page_size ||
      request.page_size > 200 || request.offset > 4096)
    return rejected<Page>();
  Page page;
  std::size_t visible = 0;
  std::string fingerprint;
  // offset 是已授权的匹配项位置，不暴露隐藏项的总数/索引。
  for (std::size_t i = 0; i < registry_->size(); ++i) {
    auto value = entry(static_cast<std::uint32_t>(i));
    if (!value)
      return foundation::make_unexpected(value.error());
    const auto &d = value->definition->description();
    auto status =
        session.inspect_catalog(caller, {d.key, d.contract_digest}, target);
    if (!status)
      return foundation::make_unexpected(status.error());
    if (!status->visible || !d.key.name.view().starts_with(request.prefix))
      continue;
    if (visible++ < request.offset)
      continue;
    if (page.items.size() == request.page_size) {
      page.next_offset = request.offset + page.items.size();
      break;
    }
    value->eligible = status->eligible;
    fingerprint += fingerprints_[i];
    fingerprint += status->eligible ? '1' : '0';
    page.items.push_back(std::move(*value));
  }
  auto digest = hash(fingerprint);
  if (!digest)
    return foundation::make_unexpected(digest.error());
  page.fingerprint = std::move(*digest);
  return page;
}
Result<std::string> Catalog::help(const Description &entry, std::size_t max) {
  if (!entry.installed || !entry.visible)
    return rejected<std::string>();
  const auto &d = entry.definition->description();
  if (d.docs.size() > max || d.key.name.view().size() > max - d.docs.size() ||
      d.key.version.text().size() + 4 >
          max - d.docs.size() - d.key.name.view().size())
    return rejected<std::string>();
  std::string text(d.key.name.view());
  text += ' ';
  text.append(d.key.version.text());
  text += '\n';
  text += d.docs;
  auto append = [&](std::string_view part) {
    if (part.size() > max - text.size())
      return false;
    text.append(part);
    return true;
  };
  auto properties = entry.args_schema.view().at("properties");
  for (std::size_t i = 0; i < properties.size(); ++i) {
    auto name = *properties.key_at(i);
    auto field = properties.at(name);
    if (!append("\n--") || !append(name))
      return rejected<std::string>();
    for (auto key : {"description", "x-unit", "format"}) {
      auto value = field.at(key).string();
      if (value &&
          (!append(" ") || !append(key) || !append("=") || !append(*value)))
        return rejected<std::string>();
    }
    if (field.at("x-explicit-validator").boolean() == true &&
        !append(" explicit-validator"))
      return rejected<std::string>();
  }
  return text;
}
} // namespace ock::catalog
