#pragma once
// NativeSubset 唯一授权实现声明。
#include <limits>
#include <ock/contracts/observation.hpp>
#include <ock/contracts/operation.hpp>
namespace ock::runtime::policy {
using namespace contracts;
using ObjectId = foundation::ObjectId;
using TimePoint = std::chrono::steady_clock::time_point;
#define OCK_POLICY_ID(N)                                                       \
  struct N##Tag;                                                               \
  using N = foundation::Tagged128<N##Tag>
OCK_POLICY_ID(StoreId);
OCK_POLICY_ID(ConnectionId);
OCK_POLICY_ID(ActionId);
OCK_POLICY_ID(WatchKey);
OCK_POLICY_ID(ResponseId);
OCK_POLICY_ID(TargetInstanceId);
#undef OCK_POLICY_ID
struct PolicyBudget {
  std::size_t principals = 128, rules = 4096, targets = 4096, sessions = 128;
  std::size_t active_actions = 4096, active_responses = 1024,
              active_watches = 256, members = 256;
  std::size_t inline_bindings = 128, active_inline_calls = 128;
  std::size_t watches_per_session = 32, watches_per_principal = 128,
              diagnostics = 128;
  std::size_t declarations = 32768, text_bytes = 1048576,
              credential_bytes = 8192;
  std::size_t queued_frames = 1024, queued_bytes = 1048576, frame_bytes = 16384,
              page_size = 200, scan_limit = 2000;
  std::uint64_t identity_limit = (std::numeric_limits<std::uint64_t>::max)(),
                generation_limit = (std::numeric_limits<std::uint64_t>::max)();
  std::chrono::milliseconds session_ttl{3600000}, action_ttl{30000},
      page_ttl{300000}, queued_ttl{30000};
};
enum class PolicyErrc : std::uint32_t {
  InvalidInput = 1,
  InvalidOwner,
  InvalidAuthority,
  AuthenticationFailed,
  SessionClosed,
  StoreClosed,
  Expired,
  Denied,
  TargetUnavailable,
  PolicyNotInstalled,
  ContractMismatch,
  InvalidGroup,
  InvalidPermit,
  AlreadyConsumed,
  Cancelled,
  CursorInvalid,
  UnsupportedDelegation,
  BudgetExceeded,
  IdentityExhausted,
  GenerationExhausted,
  Busy,
  TransmissionUnknown
};
inline constexpr foundation::ErrorDomain policy_domain{"ock.policy"};
inline Error policy_error(PolicyErrc c) noexcept {
  return Error{foundation::ErrorCode::make<policy_domain>(
      static_cast<std::uint32_t>(c))};
}
inline Result<void> deny(PolicyErrc c) noexcept {
  return make_unexpected(policy_error(c));
}
enum class PrincipalKind { User, Service };
enum class AccessUse {
  Invoke,
  Catalog,
  GetSummary,
  ListSummary,
  Wait,
  CancelExecution,
  ReadResult,
  ReadLog,
  ReadAsset,
  Subscribe
};
enum class SummaryField { Identity, Owner, Parent, Phase, Progress, Facts };
struct OperationSelector {
  OperationKey operation;
  ContractDigest contract;
  bool operator==(const OperationSelector &) const = default;
};
struct ScopeRule {
  AccessUse use;
  std::optional<OperationSelector> operation;
  std::vector<Name> permissions;
  std::vector<ObjectId> targets;
  std::vector<PrincipalRef> owners;
  std::vector<SummaryField> fields;
  bool operator==(const ScopeRule &) const = default;
};
struct DelegationInput {
  std::vector<ScopeRule> rules;
  TimePoint deadline;
  bool allow_redelegation = false;
};
struct AuthenticationAttempt {
  std::vector<std::byte> credential;
};
struct AuthenticatedIdentity {
  PrincipalRef principal;
  PrincipalKind kind;
  std::optional<PrincipalRef> delegated_by;
  DelegationInput ceiling;
  TimePoint deadline;
};
class TrustedAuthenticationPort : public PortLifetime {
public:
  virtual Result<AuthenticatedIdentity>
  authenticate(const AuthenticationAttempt &) = 0;
};
class ClockPort : public PortLifetime {
public:
  virtual TimePoint now() const noexcept = 0;
};
struct PrincipalPolicyInput {
  PrincipalRef principal;
  std::vector<ScopeRule> rules;
};
struct OperationPolicyInput {
  OperationSelector operation;
  std::vector<Name> required_permissions;
  std::vector<ScopeRule> module_rules;
  bool permits_group = false;
};
struct UsePolicyInput {
  AccessUse use;
  std::vector<Name> required_permissions;
  std::vector<ScopeRule> module_rules;
};
struct TargetPolicyInput {
  ObjectId target;
  std::uint64_t lifecycle_generation;
  std::vector<ScopeRule> rules;
  std::shared_ptr<PortLifetime> lifetime_owner;
};
struct PolicyConfiguration {
  std::vector<PrincipalPolicyInput> principals;
  std::vector<OperationPolicyInput> operations;
  std::vector<UsePolicyInput> uses;
  std::vector<TargetPolicyInput> targets;
};
class PolicyStore;
class PolicyAdministration;
class SessionAuthority;
class VerifiedCaller;
class ActionAuthorization;
class ObservationAuthorization;
class WatchAuthorization;
class ResponseAuthorization;
class GroupSnapshot;
class ProjectionSnapshot;
class SendCoordinator;
class InlineAuthorization;
class InlineAdmission;
namespace detail {
struct Access;
struct InlineRecord;
}
#define OCK_POLICY_ENTITY(N)                                                   \
public:                                                                        \
  ~N();                                                                        \
  N(const N &) = delete;                                                       \
  N &operator=(const N &) = delete;                                            \
  N(N &&) = delete;                                                            \
  N &operator=(N &&) = delete;                                                 \
                                                                               \
private:                                                                       \
  friend struct detail::Access;                                                \
  struct State;                                                                \
  explicit N(std::shared_ptr<State>);                                          \
  std::shared_ptr<State> state_
struct MemberRequest {
  OperationSelector operation;
  std::vector<ObjectId> targets;
  bool operator==(const MemberRequest &) const = default;
};
struct ActionRequest {
  OperationSelector envelope;
  ObjectId anchor_target;
  std::vector<MemberRequest> members;
  TimePoint requested_deadline;
};
class GroupSnapshot final {
public:
  const OperationSelector &envelope() const noexcept;
  ObjectId anchor_target() const noexcept;
  std::span<const MemberRequest> members() const noexcept;
  OCK_POLICY_ENTITY(GroupSnapshot);
};
class TrustedGroupDigestPort : public PortLifetime {
public:
  virtual Result<ContractDigest> fingerprint(const GroupSnapshot &) = 0;
};
struct ExecutionAccessInput {
  std::shared_ptr<const ExecutionSummary> summary;
  std::vector<ObjectId> actual_targets;
};
enum class RestoreMode { Absent, Present };
struct ObservationSourceIdentity {
  HostIncarnation host;
  RestoreMode restore;
  bool operator==(const ObservationSourceIdentity &) const = default;
};
struct AccessScanRequest {
  ListRequest list;
};
struct AccessScanPage {
  std::vector<std::pair<std::uint64_t, ExecutionAccessInput>> candidates;
  std::optional<KeysetPosition> next_scan;
  HostIncarnation host;
  Name retention_scope;
};
class ExecutionAccessSourcePort : public PortLifetime {
public:
  virtual ObservationSourceIdentity identity() const noexcept = 0;
  virtual Result<ExecutionAccessInput> find(ExecutionRef) = 0;
  virtual Result<AccessScanPage> scan(const AccessScanRequest &) = 0;
};
struct PolicyAssembly {
  std::shared_ptr<PolicyStore> store;
  std::unique_ptr<PolicyAdministration> administration;
};
class PolicyStore final {
public:
  static Result<PolicyAssembly>
  create(PolicyBudget, const PolicyConfiguration &,
         std::shared_ptr<TrustedAuthenticationPort>, std::shared_ptr<ClockPort>,
         std::shared_ptr<TrustedGroupDigestPort>,
         std::shared_ptr<ExecutionAccessSourcePort>);
  Result<std::shared_ptr<SessionAuthority>> open(const AuthenticationAttempt &,
                                                 const DelegationInput &);
  OCK_POLICY_ENTITY(PolicyStore);
};
class PolicyAdministration final {
public:
  Result<void> replace_principal_policy(const PrincipalPolicyInput &);
  Result<void> replace_operation_policy(const OperationPolicyInput &);
  Result<void> replace_use_policy(const UsePolicyInput &);
  Result<void> replace_target_policy(const TargetPolicyInput &);
  Result<void> retire_target(ObjectId);
  Result<void> set_lifecycle(ObjectId, std::uint64_t);
  Result<void> close_store();
  OCK_POLICY_ENTITY(PolicyAdministration);
};
class VerifiedCaller final {
public:
  const CallerView &view() const noexcept;
  std::shared_ptr<CallerAuthorityPort> authority() const noexcept;
  OCK_POLICY_ENTITY(VerifiedCaller);
};
class SessionAuthority final {
public:
  const PolicyBudget& limits() const noexcept;
  struct CatalogStatus { bool visible=false, eligible=false; };
  // 只读提示，不分配 Action、不发放许可；每次读取按当前政策判断。
  Result<CatalogStatus> inspect_catalog(const VerifiedCaller&,const OperationSelector&,ObjectId) const;
  Result<std::shared_ptr<const VerifiedCaller>>
  verify(const CallerDescription &);
  std::shared_ptr<CallerAuthorityPort> callers() const noexcept;
  std::shared_ptr<TargetAuthorityPort> targets() const noexcept;
  std::shared_ptr<ObservationAuthorization> observations() const noexcept;
  Result<void> restrict_delegation(const DelegationInput &);
  Result<void> close();
  Result<std::shared_ptr<ActionAuthorization>> prepare(const VerifiedCaller &,
                                                       const ActionRequest &);
  Result<std::shared_ptr<const InlineAuthorization>> prepare_inline(
      const VerifiedCaller &,
      std::shared_ptr<const DefinitionSnapshot> catalog_definition,
      std::span<const std::shared_ptr<const TargetView>> original_targets,
      std::size_t maximum_concurrent_calls);
  OCK_POLICY_ENTITY(SessionAuthority);
};
class InlineAdmission final {
public:
  InlineAdmission(InlineAdmission &&) noexcept;
  InlineAdmission &operator=(InlineAdmission &&) noexcept;
  InlineAdmission(const InlineAdmission &) = delete;
  InlineAdmission &operator=(const InlineAdmission &) = delete;
  ~InlineAdmission();
  TimePoint deadline() const noexcept;
private:
  friend class InlineAuthorization;
  friend struct detail::Access;
  explicit InlineAdmission(std::shared_ptr<detail::InlineRecord>,
                           std::size_t slot, TimePoint) noexcept;
  std::shared_ptr<detail::InlineRecord> record_;
  std::size_t slot_;
  TimePoint deadline_;
};
class InlineAuthorization final {
public:
  Result<InlineAdmission> admit(
      const DefinitionSnapshot &expected_catalog_definition,
      std::span<const ObjectId> actual_targets, std::stop_token,
      TimePoint requested_deadline) const;
  OCK_POLICY_ENTITY(InlineAuthorization);
};
class ActionAuthorization final : public PermitAuthorityPort {
public:
  Result<std::shared_ptr<const ActionPermit>> issue();
  Result<std::shared_ptr<const ActionPermit>>
  issue(const CallerGrant &, const PermitBinding &) override;
  Result<PermitBinding> current_expected_binding() const;
  Result<void> consume(const ActionPermit &, const PermitBinding &) override;
  Result<void> cancel();
  OCK_POLICY_ENTITY(ActionAuthorization);
};
struct PageBindingData {
  StoreId store;
  ConnectionId connection;
  std::uint64_t delegation_generation, permission_generation;
  PrincipalRef owner;
  PhaseSet phases;
  ListBudget budget;
  HostIncarnation host;
  RestoreMode restore;
  std::uint64_t upper_ordinal, before_ordinal;
  TimePoint deadline;
};
class PageBinding final {
public:
  PageBinding(const PageBinding &) = default;
  PageBinding &operator=(const PageBinding &) = default;
  const PageBindingData &value() const noexcept { return value_; }

private:
  friend struct detail::Access;
  explicit PageBinding(PageBindingData d) : value_(std::move(d)) {};
  PageBindingData value_;
};
// 可信装配的无状态续页校验器；返回位置不授予读取权限，list 仍重验全部政策。
class PageContinuationPort : public PortLifetime {
public:
  virtual Result<KeysetPosition> restore(const PageBindingData &) = 0;
};
enum class ProjectionKind { Summary, Page, Hint };
class ProjectionSnapshot final {
public:
  ProjectionKind kind() const noexcept;
  const std::variant<std::shared_ptr<const ExecutionSummary>, ListPage,
                     ChangeHint> &
  value() const noexcept;
  OCK_POLICY_ENTITY(ProjectionSnapshot);
};
class ResponseAuthorization final {
public:
  const ProjectionSnapshot &projection() const noexcept;
  AccessUse use() const noexcept;
  TimePoint deadline() const noexcept;
  OCK_POLICY_ENTITY(ResponseAuthorization);
};
struct AuthorizedSummary {
  std::shared_ptr<const ExecutionSummary> summary;
  std::shared_ptr<const ResponseAuthorization> response;
};
struct AuthorizedPage {
  ListPage page;
  std::optional<PageBinding> continuation;
  std::shared_ptr<const ResponseAuthorization> response;
};
class WatchAuthorization final {
public:
  WatchKey key() const noexcept;
  std::uint64_t generation() const noexcept;
  const ObservationFilter &filter() const noexcept;
  TimePoint deadline() const noexcept;
  OCK_POLICY_ENTITY(WatchAuthorization);
};
class ObservationAuthorization final {
public:
  Result<PageBinding> resume(const VerifiedCaller &, const ListRequest &,
                            PageContinuationPort &);
  Result<AuthorizedSummary> get(const VerifiedCaller &, ExecutionRef,
                                AccessUse);
  Result<AuthorizedPage> list(const VerifiedCaller &, const ListRequest &,
                              const std::optional<PageBinding> &);
  Result<std::shared_ptr<WatchAuthorization>>
  subscribe(const VerifiedCaller &, const ObservationFilter &);
  OCK_POLICY_ENTITY(ObservationAuthorization);
};
enum class StartResult { NotStarted, Started, Unknown };
struct WatchStamp {
  WatchKey key;
  std::uint64_t generation;
};
struct TransmissionBinding {
  StoreId store;
  ConnectionId connection;
  std::variant<WatchStamp, ResponseId> origin;
  TimePoint deadline;
};
class PreparedTransmission final {
public:
  std::span<const std::byte> bytes() const noexcept { return bytes_; }
  const TransmissionBinding &binding() const noexcept { return binding_; }
  const ProjectionSnapshot &projection() const noexcept { return *projection_; }
  PreparedTransmission(const PreparedTransmission &) = delete;
  PreparedTransmission &operator=(const PreparedTransmission &) = delete;

private:
  friend struct detail::Access;
  PreparedTransmission(std::vector<std::byte> b, TransmissionBinding t,
                       std::shared_ptr<const ProjectionSnapshot> p)
      : bytes_(std::move(b)), binding_(std::move(t)),
        projection_(std::move(p)) {}
  const std::vector<std::byte> bytes_;
  const TransmissionBinding binding_;
  const std::shared_ptr<const ProjectionSnapshot> projection_;
};
class ProjectionEncoderPort : public PortLifetime {
public:
  virtual Result<std::vector<std::byte>> encode(const ProjectionSnapshot &,
                                                std::size_t max_bytes) = 0;
};
class TransmissionReservation : public PortLifetime {
public:
  virtual std::size_t capacity() const noexcept = 0;

protected:
  TransmissionReservation() = default;
};
class TransmissionStartPort : public PortLifetime {
public:
  virtual Result<std::unique_ptr<TransmissionReservation>>
  reserve(std::size_t) = 0;
  virtual StartResult start_now(const PreparedTransmission &,
                                TransmissionReservation &) noexcept = 0;
};
class SendCoordinator final {
public:
  static Result<std::unique_ptr<SendCoordinator>>
      create(std::shared_ptr<const SessionAuthority>,
             std::shared_ptr<TransmissionStartPort>,
             std::shared_ptr<ProjectionEncoderPort>);
  Result<void> enqueue(const WatchAuthorization &, const ChangeHint &);
  Result<void> enqueue_response(std::shared_ptr<const ResponseAuthorization>);
  Result<StartResult> start_next();
  Result<bool> unsubscribe(const WatchAuthorization &);
  OCK_POLICY_ENTITY(SendCoordinator);
};
#undef OCK_POLICY_ENTITY
} // namespace ock::runtime::policy
