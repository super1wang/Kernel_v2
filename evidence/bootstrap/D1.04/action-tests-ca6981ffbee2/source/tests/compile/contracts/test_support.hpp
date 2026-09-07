#pragma once
// D1.02：只用于合同消费者的确定性材料，不是生产Registry或认证器。
#include <iostream>
#include <map>
#include <ock/contracts/operation.hpp>
#include <set>
#include <stdexcept>
#include <thread>
using namespace ock::contracts;
namespace foundation = ock::foundation;
inline void check(bool x, const char *text) {
  if (!x)
    throw std::runtime_error(text);
}
#define CHECK(...) check(bool((__VA_ARGS__)), #__VA_ARGS__)
inline Name name(const char *text) { return *Name::parse(text); }
inline OperationVersion ver(const char *s = "1.0.0") {
  return *OperationVersion::parse(s, 128);
}
inline OperationKey key() { return {name("test.read"), ver()}; }
template <class T> T id(unsigned value = 1) {
  T x{};
  x.bytes[15] = static_cast<std::uint8_t>(value);
  return x;
}
inline AtomicDomainRef domain(unsigned value = 1) {
  return {name("provider"), id<foundation::ObjectId>(value),
          id<foundation::RegistryGeneration>()};
}
struct PretendOwnership {
  constexpr operator AsyncOwnership() const {
    return AsyncOwnership::Disallowed;
  }
  constexpr bool operator==(AsyncOwnership) const { return true; }
};
struct MutableDeclaration { int value; };
struct Misdeclared {
  int value;
};
struct Other {
  int value;
};
struct Borrowed {
  std::string_view text;
};
struct Own {
  std::string text;
};
namespace ock::contracts {
template <> struct TypeContract<int> {
  static TypeIdentity identity() {
    return {
        *Name::parse("test.integer"), *OperationVersion::parse("1.0.0", 5), {}};
  }
  static Result<void> validate(const int &n) {
    return n >= 0 ? Result<void>{} : reject(ContractsErrc::InvalidContract);
  }
  static constexpr auto async_ownership = AsyncOwnership::Owning;
};
template <> struct TypeContract<MutableDeclaration> {
  static TypeIdentity identity() { return TypeContract<int>::identity(); }
  static Result<void> validate(const MutableDeclaration &) { return {}; }
  static inline AsyncOwnership async_ownership = AsyncOwnership::Owning;
};
template <> struct TypeContract<Misdeclared> {
  static TypeIdentity identity() { return TypeContract<int>::identity(); }
  static Result<void> validate(const Misdeclared &) { return {}; }
  static constexpr PretendOwnership async_ownership{};
};
template <> struct TypeContract<Other> {
  static TypeIdentity identity() { return TypeContract<int>::identity(); }
  static Result<void> validate(const Other &) { return {}; }
  static constexpr auto async_ownership = AsyncOwnership::Owning;
};
template <> struct TypeContract<Borrowed> {
  static TypeIdentity identity() { return TypeContract<int>::identity(); }
  static Result<void> validate(const Borrowed &) { return {}; }
};
template <> struct TypeContract<Own> {
  static TypeIdentity identity() {
    return {*Name::parse("test.own"), *OperationVersion::parse("1.0.0", 5), {}};
  }
  static Result<void> validate(const Own &) { return {}; }
  static constexpr auto async_ownership = AsyncOwnership::Owning;
};
template <> struct TypeContract<std::unique_ptr<int>> {
  static TypeIdentity identity() {
    return {
        *Name::parse("test.move"), *OperationVersion::parse("1.0.0", 5), {}};
  }
  static Result<void> validate(const std::unique_ptr<int> &p) {
    return p ? Result<void>{} : reject(ContractsErrc::InvalidContract);
  }
  static constexpr auto async_ownership = AsyncOwnership::Owning;
};
} // namespace ock::contracts
struct Reader {
  int value = 7;
  int read() const { return value; }
};
struct Provider {
  struct EditPort {
    int value = 0;
    void set(int v) { value = v; }
  };
  struct CandidateReadPort {
    int value = 0;
    int read() const { return value; }
  };
  struct Frame {
    EditPort edit;
    CandidateReadPort read;
  };
};
struct OtherProvider : Provider {};
namespace ock::contracts {
template <> struct ProviderContract<Provider> {
  static AtomicProviderKey key() { return *Name::parse("provider"); }
};
template <> struct ProviderContract<OtherProvider> {
  static AtomicProviderKey key() { return *Name::parse("provider"); }
};
} // namespace ock::contracts
inline Result<int> read_handler(const int &a, WorkContext &,
                                ReadServices<Reader> &s) {
  return a + s.reader().read();
}
inline Result<int> compute_handler(const int &a, WorkContext &) { return a; }
inline Result<int> edit_handler(const int &a, EditView<Provider> &v,
                                WorkContext &) {
  v.edit().set(a);
  return a;
}
inline Result<int> candidate_handler(const int &a,
                                     const Provider::CandidateReadPort &v,
                                     WorkContext &) {
  return a + v.read();
}
inline Result<Other> other_handler(const Other &a, WorkContext &) { return a; }
inline Result<Own> own_handler(const Own &a, WorkContext &) {
  return Own{a.text};
}
inline Result<void> void_handler(const int &, WorkContext &) { return {}; }
inline Result<std::unique_ptr<int>> move_handler(const int &n, WorkContext &) {
  return std::make_unique<int>(n);
}
inline DefinitionInput input(AtomicMode mode = AtomicMode::Incompatible) {
  return {key(), {}, {true, false, false, name("test"), name("app")},
          mode,  {}, "测试",
          4096};
}
inline WorkContext work() {
  return {{},
          std::chrono::steady_clock::now() + std::chrono::seconds(10),
          *foundation::CheckedCount<std::uint64_t>::create(0, 10),
          name("test"),
          {}};
}
class Directory final : public BindingPort {
public:
  foundation::RegistryId registry = id<foundation::RegistryId>();
  foundation::RegistryGeneration gen = id<foundation::RegistryGeneration>();
  mutable unsigned accesses = 0;
  OperationHandle handle{registry, gen, 0};
  std::shared_ptr<const DefinitionSnapshot> def;
  explicit Directory(std::shared_ptr<const DefinitionSnapshot> d)
      : def(std::move(d)) {}
  foundation::RegistryId identity() const noexcept override { return registry; }
  foundation::RegistryGeneration generation() const noexcept override {
    return gen;
  }
  std::size_t size() const noexcept override { return 1; }
  Result<OperationHandle> find(const OperationKey &) const override {
    return handle;
  }
  Result<std::shared_ptr<const DefinitionSnapshot>>
  describe(std::uint32_t slot) const override {
    ++accesses;
    if (slot != 0)
      return make_unexpected(error(ContractsErrc::StaleBinding));
    return def;
  }
};
inline std::shared_ptr<Directory> directory() {
  auto d =
      make_compute_definition(compute_handler, input(AtomicMode::PureCompute));
  CHECK(d);
  return std::make_shared<Directory>(d->snapshot());
}
inline FactBudget fact_budget() { return {32, 16, 4096}; }
inline std::shared_ptr<const KnownFacts> facts(std::vector<Fact> f = {}) {
  auto r = KnownFacts::create(f, fact_budget());
  CHECK(r);
  return *r;
}
inline CommitFact commit(unsigned fid = 1) {
  return {id<FactId>(fid), id<CommitId>(), domain(), 1,
          CommitDurability::Memory};
}
inline PublishedFact published(unsigned fid = 2) {
  return {id<FactId>(fid), id<CommitId>(), 1};
}
class Publication final : public PublicationAuthorityPort {
  class Proof final : public PublicationProof {
public:
  explicit Proof(PublishedCommit c) : value(std::move(c)) {}
  const PublishedCommit value;
};
  std::vector<std::shared_ptr<const Proof>> issued;
public:
  void publish(PublishedCommit c) {
    issued.push_back(std::make_shared<const Proof>(std::move(c)));
  }
  Result<std::shared_ptr<const PublicationProof>>
  attest(const PublishedCommit &c) override {
    for (auto &p : issued)
      if (p->value == c)
        return std::shared_ptr<const PublicationProof>(p);
    return make_unexpected(error(ContractsErrc::InvalidProof));
  }
  Result<void> validate(const PublicationProof &p,
                        const PublishedCommit &c) const override {
    for (auto &issued_proof : issued)
      if (issued_proof.get() == &p && issued_proof->value == c)
        return {};
    return reject(ContractsErrc::InvalidProof);
  }
};
inline OutcomeConditions conditions() {
  return {{}, {}, {RequiredRecordState::NotRequired, 0, 0}};
}
inline PublishedCommit publication() {
  return {id<CommitId>(), domain(), 1, 1};
}
