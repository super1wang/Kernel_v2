#include "registry.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
namespace ock::runtime::registry {
namespace {
template <class T> bool contains(const std::vector<T> &v, const T &x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}
template <class T> bool unique(const std::vector<T> &v) {
  for (std::size_t i = 0; i < v.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (v[i] == v[j])
        return false;
  return true;
}
template <class T> bool unique_refs(const std::vector<T> &v) {
  for (std::size_t i = 0; i < v.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (v[i].ref == v[j].ref)
        return false;
  return true;
}
std::atomic<std::uint64_t> next_id{1};
Result<RegistryId> issue_id() {
  auto n = next_id.load();
  for (;;) {
    if (n == std::numeric_limits<std::uint64_t>::max())
      return make_unexpected(registry_error(RegistryErrc::InvalidIdentity));
    if (next_id.compare_exchange_weak(n, n + 1))
      break;
  }
  RegistryId id{};
  for (unsigned i = 0; i < 8; ++i)
    id.bytes[15 - i] = static_cast<std::uint8_t>(n >> (8 * i));
  return id;
}
} // namespace
Catalog::Catalog(RegistryId id, std::vector<detail::HotEntry> h,
                 std::vector<std::shared_ptr<const DefinitionSnapshot>> c,
                 std::vector<std::shared_ptr<const void>> owners, std::vector<Name> module_order)
    : id_(id), hot_(std::move(h)), cold_(std::move(c)),
      owners_(std::move(owners)), module_order_(std::move(module_order)) {
  generation_.bytes[15] = 1;
}
Result<OperationHandle> Catalog::find(const OperationKey &k) const {
  for (std::size_t i = 0; i < cold_.size(); ++i)
    if (cold_[i]->description().key == k)
      return OperationHandle{id_, generation_, static_cast<std::uint32_t>(i)};
  return make_unexpected(error(ContractsErrc::StaleBinding));
}
Result<std::shared_ptr<const DefinitionSnapshot>>
Catalog::describe(std::uint32_t s) const {
  if (s >= cold_.size())
    return make_unexpected(error(ContractsErrc::StaleBinding));
  return cold_[s];
}
Result<std::unique_ptr<RegistrationBatch>>
RegistrationBatch::create(BatchBudget b) {
  if (!b.modules || !b.operations || !b.declarations || !b.text_bytes ||
      !b.diagnostics ||
      b.operations > std::numeric_limits<std::uint32_t>::max())
    return make_unexpected(registry_error(RegistryErrc::BudgetExceeded));
  return std::unique_ptr<RegistrationBatch>(new RegistrationBatch(b));
}
Result<void> RegistrationBatch::fail(RegistryErrc c, const Name *m,
                                     const OperationKey *k) noexcept {
  // 粘性位必须先于任何可能分配的诊断构造。
  failed_ = true;
  if (state_ != State::Published)
    state_ = State::Failed;
  try {
    if (errors_.size() < budget_.diagnostics)
      errors_.push_back({c, m ? std::optional<Name>(*m) : std::nullopt,
                         k ? std::optional<OperationKey>(*k) : std::nullopt});
    else
      truncated_ = true;
  } catch (...) {
    truncated_ = true;
  }
  return make_unexpected(registry_error(c));
}
void RegistrationBatch::release_candidates() noexcept {
  hot_.clear();
  cold_.clear();
  modules_.clear();
}
Result<void> RegistrationBatch::add(const ModuleInput &m) {
  try {
    if (state_ != State::Collecting)
      return fail(RegistryErrc::Frozen);
    auto &x = m.manifest;
    auto charge = [&](std::size_t &used, std::size_t n, std::size_t max) {
      if (n > max - used) {
        fail(RegistryErrc::BudgetExceeded, &x.name);
        return false;
      }
      used += n;
      return true;
    };
    if (modules_.size() >= budget_.modules)
      return fail(RegistryErrc::BudgetExceeded, &x.name);
    bool ok = true;
    auto text = [&](std::string_view s) {
      if (!charge(texts_, s.size(), budget_.text_bytes))
        ok = false;
    };
    auto names = [&](const auto &list) {
      if (!charge(declarations_, list.size(), budget_.declarations))
        ok = false;
      for (const auto &n : list)
        text(n.view());
    };
    text(x.name.view());
    text(x.version.text());
    if (!charge(operations_, x.operations.size(), budget_.operations))
      ok = false;
    for (const auto &k : x.operations) {
      text(k.name.view());
      text(k.version.text());
    }
    names(x.services);
    names(x.providers);
    names(x.resources);
    names(x.executors);
    if (!charge(declarations_, x.dependencies.size(), budget_.declarations))
      ok = false;
    for (auto &d : x.dependencies) {
      text(d.name.view());
      text(d.version.text());
    }
    if (!charge(declarations_, x.required_configuration.size(),
                budget_.declarations))
      ok = false;
    for (auto &c : x.required_configuration) {
      text(c.name.view());
      text(c.version.text());
    }
    auto refs = [&](const auto &list) {
      if (!charge(declarations_, list.size(), budget_.declarations))
        ok = false;
      for (auto &r : list) {
        text(r.module.view());
        text(r.name.view());
      }
    };
    refs(x.required_resources);
    refs(x.required_executors);
    if (!charge(declarations_, x.required_services.size(),
                budget_.declarations))
      ok = false;
    for (auto &r : x.required_services) {
      text(r.ref.module.view());
      text(r.ref.name.view());
    }
    if (!charge(declarations_, x.required_providers.size(),
                budget_.declarations))
      ok = false;
    for (auto &r : x.required_providers) {
      text(r.ref.module.view());
      text(r.ref.key.view());
    }
    // 装配输入也有界，避免未声明的巨大vector绕过预算。
    for (auto n : {m.services.size(), m.providers.size(), m.resources.size(),
                   m.configurations.size(), m.executors.size()})
      if (!charge(declarations_, n, budget_.declarations))
        ok = false;
    for (auto &s : m.services)
      text(s.name_.view());
    for (auto &p : m.providers)
      text(p.key_.view());
    for (auto &r : m.resources)
      text(r.name.view());
    for (auto &e : m.executors) {
      text(e.name.view());
      text(e.affinity.view());
    }
    for (auto &c : m.configurations) {
      text(c.identity_.name.view());
      text(c.identity_.version.text());
    }
    if (!ok)
      return fail(RegistryErrc::BudgetExceeded, &x.name);
    // 入口内独立复制所有容器；std::move实参也不能保留候选元素别名。
    modules_.push_back(m);
    return {};
  } catch (...) {
    fail(RegistryErrc::CallbackException);
    throw;
  }
}
std::optional<std::size_t>
RegistrationBatch::module_index(const Name &n) const {
  for (std::size_t i = 0; i < modules_.size(); ++i)
    if (modules_[i].manifest.name == n)
      return i;
  return {};
}
bool RegistrationBatch::allowed(std::size_t i, const Name &n) const {
  auto &m = modules_[i].manifest;
  if (n == m.name)
    return true;
  for (auto &d : m.dependencies)
    if (d.name == n)
      return true;
  return false;
}
bool RegistrationBatch::validate_module(std::size_t i) {
  auto &m = modules_[i];
  auto &d = m.manifest;
  bool ok = true;
  auto bad = [&](RegistryErrc c) {
    ok = false;
    fail(c, &d.name);
  };
  if (!unique(d.operations) || !unique(d.services) || !unique(d.providers) ||
      !unique(d.resources) || !unique(d.executors) ||
      !unique(d.required_configuration) || !unique(d.required_resources) ||
      !unique(d.required_executors) || !unique_refs(d.required_services) ||
      !unique_refs(d.required_providers))
    bad(RegistryErrc::InvalidManifest);
  auto exact = [&](const auto &declared, const auto &values, auto key,
                   auto valid, RegistryErrc err) {
    if (declared.size() != values.size()) {
      bad(err);
      return;
    }
    for (auto &n : declared) {
      std::size_t count = 0;
      for (auto &v : values)
        if (key(v) == n && valid(v))
          ++count;
      if (count != 1)
        bad(err);
    }
  };
  exact(
      d.services, m.services, [](auto &v) { return v.name_; },
      [](auto &v) { return bool(v.owner_); }, RegistryErrc::MissingService);
  exact(
      d.providers, m.providers, [](auto &v) { return v.key_; },
      [](auto &v) { return bool(v.owner_); }, RegistryErrc::MissingProvider);
  exact(
      d.resources, m.resources, [](auto &v) { return v.name; },
      [](auto &v) { return bool(v.owner) && v.owner.use_count() > 0; },
      RegistryErrc::MissingResource);
  exact(
      d.executors, m.executors, [](auto &v) { return v.name; },
      [](auto &v) { return bool(v.owner) && v.owner.use_count() > 0; },
      RegistryErrc::MissingExecutor);
  exact(
      d.required_configuration, m.configurations,
      [](auto &v) { return v.identity_; },
      [](auto &v) { return bool(v.owner_); },
      RegistryErrc::MissingConfiguration);
  for (auto &r : d.required_services) {
    auto j = module_index(r.ref.module);
    bool found = false;
    if (j && allowed(i, r.ref.module))
      for (auto &s : modules_[*j].services)
        if (s.name_ == r.ref.name) {
          found = true;
          if (s.type_ != r.type)
            bad(RegistryErrc::ServiceTypeMismatch);
        }
    if (!found)
      bad(RegistryErrc::MissingService);
  }
  for (auto &r : d.required_providers) {
    auto j = module_index(r.ref.module);
    bool found = false;
    if (j && allowed(i, r.ref.module))
      for (auto &s : modules_[*j].providers)
        if (s.key_ == r.ref.key) {
          found = true;
          if (s.type_ != r.type)
            bad(RegistryErrc::ProviderMismatch);
        }
    if (!found)
      bad(RegistryErrc::MissingProvider);
  }
  for (auto &r : d.required_resources) {
    auto j = module_index(r.module);
    bool found = false;
    if (j && allowed(i, r.module))
      for (auto &s : modules_[*j].resources)
        if (s.name == r.name && s.owner)
          found = true;
    if (!found)
      bad(RegistryErrc::MissingResource);
  }
  for (auto &r : d.required_executors) {
    auto j = module_index(r.module);
    bool found = false;
    if (j && allowed(i, r.module))
      for (auto &s : modules_[*j].executors)
        if (s.name == r.name && s.owner)
          found = true;
    if (!found)
      bad(RegistryErrc::MissingExecutor);
  }
  return ok;
}
Result<std::shared_ptr<void>>
RegistrationBatch::service(std::size_t i, const ServiceRef &r, CppTypeToken t) {
  if (state_ != State::Validating || failed_) {
    fail(RegistryErrc::Frozen);
    return make_unexpected(registry_error(RegistryErrc::Frozen));
  }
  auto reject_service = [&](RegistryErrc c) -> Result<std::shared_ptr<void>> {
    fail(c, &modules_[i].manifest.name);
    return make_unexpected(registry_error(c));
  };
  bool declared = false;
  for (auto &q : modules_[i].manifest.required_services)
    if (q.ref == r && q.type == t)
      declared = true;
  auto j = module_index(r.module);
  if (!declared || !j || !allowed(i, r.module))
    return reject_service(RegistryErrc::MissingService);
  for (auto &s : modules_[*j].services)
    if (s.name_ == r.name) {
      if (s.type_ != t)
        return reject_service(RegistryErrc::ServiceTypeMismatch);
      return s.owner_;
    }
  return reject_service(RegistryErrc::MissingService);
}
Result<void> RegistrationBatch::preflight(std::size_t i,
                                          const DefinitionInput &d,
                                          const OperationOptions &o) {
  auto bad = [&] {
    return fail(RegistryErrc::BudgetExceeded, &modules_[i].manifest.name,
                &d.key);
  };
  std::size_t text = texts_, count = declarations_;
  auto charge = [](std::size_t &n, std::size_t extra, std::size_t limit) {
    if (extra > limit - n)
      return false;
    n += extra;
    return true;
  };
  auto t = [&](std::string_view v) {
    return charge(text, v.size(), budget_.text_bytes);
  };
  if (!charge(count, d.required_permissions.size(), budget_.declarations) ||
      !charge(count, o.resources.size(), budget_.declarations))
    return bad();
  if (!t(d.key.name.view()) || !t(d.key.version.text()) || !t(d.docs) ||
      !t(d.execution.executor.view()) ||
      !t(d.execution.thread_affinity.view()) || !t(o.executor.module.view()) ||
      !t(o.executor.name.view()))
    return bad();
  for (auto &p : d.required_permissions)
    if (!t(p.view()))
      return bad();
  for (auto &r : o.resources)
    if (!t(r.module.view()) || !t(r.name.view()))
      return bad();
  if (o.read_service &&
      (!charge(count, 1, budget_.declarations) ||
       !t(o.read_service->module.view()) || !t(o.read_service->name.view())))
    return bad();
  if (o.provider &&
      (!charge(count, 1, budget_.declarations) ||
       !t(o.provider->module.view()) || !t(o.provider->key.view())))
    return bad();
  texts_ = text;
  declarations_ = count;
  return {};
}
Result<void> RegistrationBatch::insert(
    std::size_t i, std::shared_ptr<const DefinitionSnapshot> s,
    const OperationOptions &o, std::shared_ptr<const void> handler,
    CppTypeToken ht, detail::HotEntry::NativeThunk native) {
  auto &m = modules_[i].manifest;
  auto &d = s->description();
  auto bad = [&](RegistryErrc c) { return fail(c, &m.name, &d.key); };
  // 工厂生成的真实类型身份也属于冻结文本；可信TypeContract的临时
  // 分配无法在调用前预测，但超预算的结果绝不能进入候选目录。
  std::size_t snapshot_text = texts_;
  auto charge_identity = [&](std::string_view text) {
    if (text.size() > budget_.text_bytes - snapshot_text)
      return false;
    snapshot_text += text.size();
    return true;
  };
  if (!charge_identity(s->args_contract().name.view()) ||
      !charge_identity(s->args_contract().version.text()) ||
      !charge_identity(s->result_contract().name.view()) ||
      !charge_identity(s->result_contract().version.text()) ||
      (s->provider() && !charge_identity(s->provider()->view())))
    return bad(RegistryErrc::BudgetExceeded);
  texts_ = snapshot_text;
  if (!contains(m.operations, d.key))
    return bad(RegistryErrc::UndeclaredOperation);
  for (auto &old : cold_)
    if (old->description().key == d.key)
      return bad(RegistryErrc::DuplicateOperation);
  if (o.requires_dynamic_schema)
    return bad(RegistryErrc::UnsupportedSchema);
  if (!unique(o.resources))
    return bad(RegistryErrc::InvalidManifest);
  const bool read =
      s->shape() == Shape::Read && d.atomic_mode != AtomicMode::PureCompute;
  const bool provider = d.atomic_mode == AtomicMode::StateEdit ||
                        d.atomic_mode == AtomicMode::CandidateRead;
  if (bool(o.read_service) != read || bool(o.provider) != provider)
    return bad(RegistryErrc::InvalidDefinition);
  if ((d.atomic_mode != AtomicMode::Incompatible) &&
      (d.execution.requires_async_dispatch ||
       d.execution.requires_external_wait))
    return bad(RegistryErrc::CapabilityMismatch);
  detail::HotEntry h{s->shape(),
                     std::move(handler),
                     ht,
                     {},
                     d.execution.inline_safe,
                     d.execution.requires_async_dispatch,
                     d.execution.requires_external_wait,
                     native,
                     {},
                     CppTypeToken::of<void>(),
                     o.resources};
  if (read) {
    auto owner = service(i, *o.read_service, s->context_type());
    if (!owner)
      return make_unexpected(owner.error());
    h.owners.push_back(*owner);
    h.reader_owner = *owner;
    h.reader_type = s->context_type();
  }
  if (provider) {
    auto j = module_index(o.provider->module);
    bool declared = false;
    for (auto &r : m.required_providers)
      if (r.ref == *o.provider && s->provider_type() &&
          r.type == *s->provider_type())
        declared = true;
    if (!j || !allowed(i, o.provider->module) || !declared)
      return bad(RegistryErrc::MissingProvider);
    bool found = false;
    for (auto &p : modules_[*j].providers)
      if (p.key_ == o.provider->key) {
        if (!s->provider() || p.key_ != *s->provider() ||
            p.type_ != *s->provider_type())
          return bad(RegistryErrc::ProviderMismatch);
        h.owners.push_back(p.owner_);
        found = true;
      }
    if (!found)
      return bad(RegistryErrc::MissingProvider);
  }
  auto j = module_index(o.executor.module);
  if (!j || !allowed(i, o.executor.module) ||
      o.executor.name != d.execution.executor)
    return bad(RegistryErrc::MissingExecutor);
  if (o.executor.module != m.name &&
      !contains(m.required_executors, o.executor))
    return bad(RegistryErrc::MissingExecutor);
  bool found = false;
  for (auto &e : modules_[*j].executors)
    if (e.name == o.executor.name) {
      if (!e.owner || e.affinity != d.execution.thread_affinity ||
          (d.execution.requires_async_dispatch && !e.async_dispatch) ||
          (d.execution.requires_external_wait && !e.external_wait))
        return bad(RegistryErrc::CapabilityMismatch);
      h.owners.push_back(e.owner);
      found = true;
    }
  if (!found)
    return bad(RegistryErrc::MissingExecutor);
  for (auto &r : o.resources) {
    j = module_index(r.module);
    if (!j || !allowed(i, r.module) ||
        (r.module != m.name && !contains(m.required_resources, r)))
      return bad(RegistryErrc::MissingResource);
    found = false;
    for (auto &v : modules_[*j].resources)
      if (v.name == r.name && v.owner) {
        h.owners.push_back(v.owner);
        found = true;
      }
    if (!found)
      return bad(RegistryErrc::MissingResource);
  }
  hot_.push_back(std::move(h));
  cold_.push_back(std::move(s));
  return {};
}
Result<std::shared_ptr<const Catalog>> RegistrationBatch::publish() {
  if (publishing_) {
    fail(RegistryErrc::Frozen);
    return make_unexpected(registry_error(RegistryErrc::Frozen));
  }
  publishing_ = true;
  struct PublishingGuard {
    bool &active;
    ~PublishingGuard() { active = false; }
  } guard{publishing_};
  auto rejected = [&]() -> Result<std::shared_ptr<const Catalog>> {
    release_candidates();
    return make_unexpected(registry_error(RegistryErrc::InvalidManifest));
  };
  try {
    if (state_ != State::Collecting) {
      fail(RegistryErrc::Frozen);
      return rejected();
    }
    state_ = State::Validating;
    for (std::size_t i = 0; i < modules_.size(); ++i)
      for (auto &k : modules_[i].manifest.operations)
        for (std::size_t j = 0; j < i; ++j)
          if (contains(modules_[j].manifest.operations, k))
            fail(RegistryErrc::DuplicateOperation, &modules_[i].manifest.name,
                 &k);
    for (std::size_t i = 0; i < modules_.size(); ++i) {
      auto &m = modules_[i].manifest;
      for (std::size_t j = 0; j < i; ++j)
        if (modules_[j].manifest.name == m.name)
          fail(RegistryErrc::DuplicateModule, &m.name);
      for (std::size_t k = 0; k < m.dependencies.size(); ++k) {
        auto &d = m.dependencies[k];
        for (std::size_t z = 0; z < k; ++z)
          if (m.dependencies[z].name == d.name)
            fail(RegistryErrc::InvalidManifest, &m.name);
        auto j = module_index(d.name);
        if (!j)
          fail(RegistryErrc::MissingDependency, &m.name);
        else if (modules_[*j].manifest.version != d.version)
          fail(RegistryErrc::VersionMismatch, &m.name);
      }
    }
    if (failed_)
      return rejected();
    std::vector<std::size_t> order;
    std::vector<bool> done(modules_.size(), false);
    while (order.size() < modules_.size()) {
      std::optional<std::size_t> selected;
      for (std::size_t i = 0; i < modules_.size(); ++i)
        if (!done[i]) {
          bool ready = true;
          for (auto &d : modules_[i].manifest.dependencies)
            if (!done[*module_index(d.name)])
              ready = false;
          if (ready &&
              (!selected || modules_[i].manifest.name.view() <
                                modules_[*selected].manifest.name.view()))
            selected = i;
        }
      if (!selected) {
        fail(RegistryErrc::DependencyCycle);
        return rejected();
      }
      done[*selected] = true;
      order.push_back(*selected);
    }
    // 先验证全部装配，任何失败都不会运行依赖缺失的钩子。
    for (auto i : order)
      validate_module(i);
    if (failed_)
      return rejected();
    for (auto i : order) {
      auto &m = modules_[i];
      const auto first_slot = cold_.size();
      Registrar r(*this, i);
      try {
        if (m.register_operations)
          m.register_operations(r);
      } catch (...) {
        fail(RegistryErrc::CallbackException, &m.manifest.name);
      }
      for (auto &k : m.manifest.operations) {
        bool present = false;
        for (auto slot = first_slot; slot < cold_.size(); ++slot)
          if (cold_[slot]->description().key == k)
            present = true;
        if (!present)
          fail(RegistryErrc::MissingOperation, &m.manifest.name, &k);
      }
      if (failed_)
        break;
    }
    if (failed_)
      return rejected();
    auto id = issue_id();
    if (!id) {
      fail(RegistryErrc::InvalidIdentity);
      return rejected();
    }
    std::vector<std::shared_ptr<const void>> owners;
    for (auto &m : modules_) {
      for (auto &s : m.services)
        owners.push_back(s.owner_);
      for (auto &p : m.providers)
        owners.push_back(p.owner_);
      for (auto &r : m.resources)
        owners.push_back(r.owner);
      for (auto &e : m.executors)
        owners.push_back(e.owner);
      for (auto &c : m.configurations)
        owners.push_back(c.owner_);
    }
    std::vector<Name> module_order;
    module_order.reserve(order.size());
    for (auto i : order) module_order.push_back(modules_[i].manifest.name);
    auto catalog = std::shared_ptr<const Catalog>(
        new Catalog(*id, std::move(hot_), std::move(cold_), std::move(owners), std::move(module_order)));
    state_ = State::Published;
    modules_.clear();
    return catalog;
  } catch (...) {
    fail(RegistryErrc::CallbackException);
    release_candidates();
    throw;
  }
}
} // namespace ock::runtime::registry
