#include <ock/contracts/operation.hpp>
#include <ock/foundation/sdk_version.hpp>
#include <memory>
#include <type_traits>
#include <variant>
struct Value { int number; };
namespace ock::contracts {
template<> struct TypeContract<Value> {
    static TypeIdentity identity() { return {*Name::parse("sdk.value"), *OperationVersion::parse("1.0.0", 32), {}}; }
    static Result<void> validate(const Value& value) { return value.number >= 0 ? Result<void>{} : reject(ContractsErrc::InvalidContract); }
    static constexpr AsyncOwnership async_ownership = AsyncOwnership::Owning;
};
}
using namespace ock::contracts;
class EmptyCatalog final : public BindingPort {
public:
    mutable unsigned descriptions = 0;
    ock::foundation::RegistryId identity() const noexcept override { return {}; }
    ock::foundation::RegistryGeneration generation() const noexcept override { return {}; }
    std::size_t size() const noexcept override { return 0; }
    Result<OperationHandle> find(const OperationKey&) const override { return OperationHandle{}; }
    Result<std::shared_ptr<const DefinitionSnapshot>> describe(std::uint32_t) const override {
        ++descriptions; return make_unexpected(error(ContractsErrc::Rejected));
    }
};
class NoPublications final : public PublicationAuthorityPort {
public:
    Result<std::shared_ptr<const PublicationProof>> attest(const PublishedCommit&) override { return make_unexpected(error(ContractsErrc::InvalidProof)); }
    Result<void> validate(const PublicationProof&, const PublishedCommit&) const override { return reject(ContractsErrc::InvalidProof); }
};
class Completion final : public CompletionPort {
public:
    unsigned calls = 0;
    Result<void> candidate_ready(Result<void> status) noexcept override { ++calls; return status; }
};
static_assert(!ock::sdk::runtime_available);
static_assert(std::variant_size_v<Outcome<Value>::Candidate> == 9);
static_assert(!std::is_default_constructible_v<BoundOperation<Value,Value>>);
static_assert(!std::is_convertible_v<ResourceLease*,ActionPermit*>);
static_assert(!std::is_convertible_v<ActivityLease*,ActionPermit*>);
int main() {
    auto version = OperationVersion::parse("123456789012345678901234567890.0.1", 64);
    if (!version || OperationVersion::parse("01.0.0", 64)) return 1;
    if (CppTypeToken::of<Value>() == CppTypeToken::of<int>()) return 2;
    auto catalog = std::make_shared<EmptyCatalog>();
    OperationKey key{*Name::parse("sdk.compute"), *OperationVersion::parse("1.0.0", 32)};
    auto bound = bind<Value,Value>(catalog, key, ContractDigest{}, Shape::Read);
    if (bound || catalog->descriptions != 0) return 3;
    NoPublications publications;
    FactBudget budget{8,8,2048};
    auto facts = KnownFacts::create({}, budget);
    if (!facts) return 4;
    OutcomeValidation validation{publications, {}, budget};
    Outcome<Value>::Candidate candidate{ReadCompleted<Value>{Value{7}}};
    auto outcome = Outcome<Value>::validate(std::move(candidate), *facts, EvidenceState::Volatile, OutcomeConditions{}, validation);
    if (!outcome) return 5;
    auto completion = std::make_shared<Completion>();
    CompletionLatch latch{completion};
    if (!latch.complete({}) || latch.complete({}) || completion->calls != 1) return 6;
    return 0;
}
