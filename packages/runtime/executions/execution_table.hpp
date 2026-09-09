#pragma once
#include <ock/contracts/observation.hpp>
#include <ock/runtime/policy.hpp>
#include <ock/runtime/host.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <condition_variable>

namespace ock::runtime::executions::detail {
// 内部拥有表。调用者身份与观察权限由 Runtime 适配器检查，不能直接安装为 SDK 入口。
class ExecutionTable final {
public:
  using Limits=host::ExecutionLimits;
  struct Usage {std::size_t records=0,input_bytes=0,reply_bytes=0,waiters=0;};
  enum class WaitState {Terminal,Timeout,Cancelled};
  using ReplyAccess=const void* (*)(const void*) noexcept;
private:
  struct Ledger {std::mutex mutex;Usage used;Limits limits;};
  struct Reservation {
    std::shared_ptr<Ledger> ledger;
    std::size_t input=0,reply=0;
    ~Reservation() {
      if(!ledger)return;
      std::lock_guard lock(ledger->mutex);
      --ledger->used.records;ledger->used.input_bytes-=input;ledger->used.reply_bytes-=reply;
    }
  };
public:
  class Entry final : public std::enable_shared_from_this<Entry> {
    friend class ExecutionTable;
  public:
    contracts::ExecutionRef execution() const noexcept {return summary_.execution;}
    std::uint64_t ordinal() const noexcept {return ordinal_;}
    // 读取返回的 owner 必须与 result 指针共同保活；索引淘汰不归还其预算。
    std::shared_ptr<const void> payload() const noexcept {
      return {shared_from_this(),payload_.get()};
    }
    contracts::CppTypeToken result_type() const noexcept {return result_type_;}
  private:
    Entry(contracts::SummaryInput summary,std::shared_ptr<void> payload,
        contracts::CppTypeToken type,std::shared_ptr<Reservation> reservation,
        std::vector<foundation::ObjectId> targets,ReplyAccess reply)
        :reservation_(std::move(reservation)),payload_(std::move(payload)),
         result_type_(type),summary_(std::move(summary)),targets_(std::move(targets)),reply_(reply) {summary_.facts.reserve(8);}
    // reservation 最后析构，确保 payload 析构及 capture 释放期间仍计费。
    std::shared_ptr<Reservation> reservation_;
    std::shared_ptr<void> payload_;
    contracts::CppTypeToken result_type_;
    contracts::SummaryInput summary_;
    std::vector<foundation::ObjectId> targets_;
    ReplyAccess reply_;
    std::condition_variable changed_;
    std::uint64_t ordinal_=0;
    bool accepted_=false;
  };
  static contracts::Result<std::shared_ptr<ExecutionTable>> create(Limits limits,contracts::HostIncarnation host) {
    if(!limits.records||!limits.input_bytes||!limits.reply_bytes||
       !limits.terminal_records||!limits.terminal_bytes||host.empty()||
       !limits.page_size||limits.page_size>200||!limits.scan_limit||!limits.targets_per_record||!limits.waiters)return failure();
    try {return std::shared_ptr<ExecutionTable>(new ExecutionTable(limits,host));}
    catch(...) {return failure();}
  }
  // summary/identity 必须由可信适配器生成；此处仍验证格式与重复身份。
  // accepted=false 的内部条目不可观察；安装失败从未发布 Accepted。
  contracts::Result<std::shared_ptr<Entry>> prepare(contracts::SummaryInput summary,
      std::shared_ptr<void> payload,contracts::CppTypeToken type,
      std::size_t input_bytes,std::size_t reply_bytes,std::vector<foundation::ObjectId> targets={},ReplyAccess reply=nullptr) {
    if(!payload||summary.phase!=contracts::ExecutionPhase::Queued||
       summary.version.value()!=1||!summary.facts.empty()||summary.host!=host_||
       targets.size()>ledger_->limits.targets_per_record)return failure();
    for(const auto& target:targets)if(target.empty())return failure();
    auto valid=contracts::validate_summary(summary);
    if(!valid)return contracts::make_unexpected(valid.error());
    try {
      auto reservation=std::make_shared<Reservation>();
      {
        std::lock_guard lock(ledger_->mutex);const auto& l=ledger_->limits;auto& u=ledger_->used;
        if(u.records>=l.records||input_bytes>l.input_bytes-u.input_bytes||
           reply_bytes>l.reply_bytes-u.reply_bytes)return failure();
        ++u.records;u.input_bytes+=input_bytes;u.reply_bytes+=reply_bytes;
        reservation->ledger=ledger_;reservation->input=input_bytes;reservation->reply=reply_bytes;
      }
      auto entry=std::shared_ptr<Entry>(new Entry(std::move(summary),std::move(payload),type,std::move(reservation),std::move(targets),reply));
      std::lock_guard lock(mutex_);
      if(closed_||next_>=(std::numeric_limits<std::uint64_t>::max)()-1||
         by_id_.contains(entry->execution().execution_id))return failure();
      entry->ordinal_=++next_; // 拒绝也不复用 ordinal。
      auto inserted=ordered_.emplace(entry->ordinal_,entry);
      try {
        by_id_.emplace(entry->execution().execution_id,entry);
        active_owners_.emplace(owner_key(*entry),entry);
      } catch(...) {by_id_.erase(entry->execution().execution_id);ordered_.erase(inserted.first);throw;}
      return entry;
    } catch(...) {return failure();}
  }
  contracts::Result<void> publish(const std::shared_ptr<Entry>& entry) {
    std::lock_guard lock(mutex_);
    if(!contains(entry)||entry->accepted_||closed_)return failure();
    entry->accepted_=true;
    if(entry->summary_.phase==contracts::ExecutionPhase::Terminal) {
      ++terminal_count_;terminal_bytes_+=entry->reservation_->reply;
    }
    return {};
  }
  // enqueue 拒绝/接受前中止：只摘除未接受条目。销毁永远在表锁外。
  bool abandon(const std::shared_ptr<Entry>& entry) {
    std::lock_guard lock(mutex_);
    if(!contains(entry)||entry->accepted_)return false;
    by_id_.erase(entry->execution().execution_id);ordered_.erase(entry->ordinal_);
    active_owners_.erase(owner_key(*entry));terminal_owners_.erase(owner_key(*entry));return true;
  }
  std::shared_ptr<Entry> find(contracts::ExecutionRef ref) const {
    std::lock_guard lock(mutex_);auto i=by_id_.find(ref.execution_id);
    return i!=by_id_.end()&&i->second->accepted_?i->second:nullptr;
  }
  contracts::Result<std::shared_ptr<const contracts::ExecutionSummary>> summary(
      const std::shared_ptr<Entry>& entry) const {
    try {
      std::lock_guard lock(mutex_);
      if(!contains(entry)||!entry->accepted_)return failure();
      return contracts::ExecutionSummary::create(entry->summary_);
    } catch(...) {return failure();}
  }
  // 完成可早于 enqueue 返回；未接受时仅积存内部事实，find 仍不可见。
  contracts::Result<void> transition(const std::shared_ptr<Entry>& entry,
      contracts::ExecutionPhase phase,const contracts::PhaseConditions& conditions,
      std::optional<contracts::Error> fault={}) {
    std::lock_guard lock(mutex_);
    if(!contains(entry))return failure();
    auto valid=contracts::validate_phase_change(entry->summary_.phase,phase,conditions);
    if(!valid)return valid;
    auto version=entry->summary_.version.next();if(!version)return contracts::make_unexpected(version.error());
    // 本窄路径尚不接收 required-record 失败；不能生成不一致的摘要。
    if(conditions.record_failure||conditions.finalization.record_state!=contracts::RequiredRecordState::NotRequired)
      return contracts::reject(contracts::ContractsErrc::InvalidFact);
    entry->summary_.phase=phase;entry->summary_.version=*version;
    if(phase==contracts::ExecutionPhase::Terminal) {
      // 接受前已预留同类 map node；终态发布只转移节点，不分配新的索引材料。
      auto node=active_owners_.extract(owner_key(*entry));
      foundation::invariant(!node.empty());terminal_owners_.insert(std::move(node));
    }
    if(phase==contracts::ExecutionPhase::Terminal&&entry->accepted_) {
      ++terminal_count_;terminal_bytes_+=entry->reservation_->reply;
    }
    if(fault)entry->summary_.fault=contracts::Error{fault->code()};
    if(phase==contracts::ExecutionPhase::Terminal)entry->changed_.notify_all();
    return {};
  }
  template<contracts::ContractResult R>
  contracts::Result<std::shared_ptr<const contracts::InvokeReply<R>>> result(contracts::ExecutionRef ref) const {
    auto reply=result_erased(ref,contracts::CppTypeToken::of<R>());
    if(!reply)return contracts::make_unexpected(reply.error());
    return std::static_pointer_cast<const contracts::InvokeReply<R>>(*reply);
  }
  contracts::Result<std::shared_ptr<const void>> result_erased(contracts::ExecutionRef ref,
      contracts::CppTypeToken type) const {
    std::lock_guard lock(mutex_);auto i=by_id_.find(ref.execution_id);
    if(i==by_id_.end()||!i->second->accepted_||i->second->summary_.phase!=contracts::ExecutionPhase::Terminal||!i->second->reply_)
      return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
    const auto& entry=i->second;
    if(entry->result_type_!=type)
      return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::TypeMismatch));
    auto reply=entry->reply_(entry->payload_.get());
    if(!reply)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
    return std::shared_ptr<const void>(entry,reply);
  }
  // 仅供经过当前授权且允许阻塞的查询适配器使用；timeout/stop 不取消 Execution。
  contracts::Result<WaitState> wait_terminal(contracts::ExecutionRef ref,
      std::chrono::steady_clock::time_point deadline,std::stop_token stop={}) {
    auto entry=find(ref);if(!entry)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
    {
      std::lock_guard lock(ledger_->mutex);
      if(ledger_->used.waiters>=ledger_->limits.waiters)return failure();
      ++ledger_->used.waiters;
    }
    struct WaitReservation {std::shared_ptr<Ledger> ledger;~WaitReservation(){std::lock_guard lock(ledger->mutex);--ledger->used.waiters;}} reservation{ledger_};
    // callback 与 wait 使用同一锁，关闭 predicate 检查和睡眠之间的 lost-wake 窗口。
    std::stop_callback cancelled(stop,[&]{std::lock_guard lock(mutex_);entry->changed_.notify_all();});
    std::unique_lock lock(mutex_);
    const auto ready=[&]{return entry->summary_.phase==contracts::ExecutionPhase::Terminal||stop.stop_requested();};
    if(!entry->changed_.wait_until(lock,deadline,ready))return WaitState::Timeout;
    return entry->summary_.phase==contracts::ExecutionPhase::Terminal?WaitState::Terminal:WaitState::Cancelled;
  }
  Usage usage() const {std::lock_guard lock(ledger_->mutex);return ledger_->used;}
  void close_admission() {std::lock_guard lock(mutex_);closed_=true;}
  contracts::HostIncarnation host() const noexcept {return host_;}
  contracts::Result<policy::ExecutionAccessInput> access_find(contracts::ExecutionRef ref) const {
    try {
      std::lock_guard lock(mutex_);auto i=by_id_.find(ref.execution_id);
      if(i==by_id_.end()||!i->second->accepted_)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
      auto summary=contracts::ExecutionSummary::create(i->second->summary_);
      if(!summary)return contracts::make_unexpected(summary.error());
      return policy::ExecutionAccessInput{*summary,i->second->targets_};
    } catch(...) {return failure();}
  }
  contracts::Result<policy::AccessScanPage> access_scan(const contracts::ListRequest& request) const {
    const auto& limits=ledger_->limits;
    if(request.owner.principal_id.empty()||request.phases>contracts::PhaseSet::All||
       !request.budget.page_size||request.budget.page_size>limits.page_size||
       !request.budget.scan_limit||request.budget.scan_limit>limits.scan_limit)return failure();
    if(request.position&&(request.position->host!=host_||!request.position->before_ordinal||
       request.position->before_ordinal>request.position->upper_ordinal))
      return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::HostMismatch));
    try {
      policy::AccessScanPage page{{},{},host_,*contracts::Name::parse("volatile.hot-cache")};
      page.candidates.reserve(std::min(request.budget.page_size,request.budget.scan_limit));
      std::lock_guard lock(mutex_);
      const auto upper=request.position?request.position->upper_ordinal:next_;
      auto before=request.position?request.position->before_ordinal:next_+1;
      const auto candidate=[&](std::uint64_t boundary) {
        auto a=request.phases==contracts::PhaseSet::Terminal?std::shared_ptr<Entry>{}:
            before_owner(active_owners_,request.owner.principal_id,boundary);
        auto t=request.phases==contracts::PhaseSet::Nonterminal?std::shared_ptr<Entry>{}:
            before_owner(terminal_owners_,request.owner.principal_id,boundary);
        return !a?t:!t?a:a->ordinal_>t->ordinal_?a:t;
      };
      for(std::uint32_t scanned=0;scanned<request.budget.scan_limit&&page.candidates.size()<request.budget.page_size;++scanned) {
        auto entry=candidate(before);if(!entry)break;
        before=entry->ordinal_; // 即使尚未接受也前进，不无限重扫。
        if(!entry->accepted_||entry->ordinal_>upper)continue;
        auto summary=contracts::ExecutionSummary::create(entry->summary_);
        if(!summary)return contracts::make_unexpected(summary.error());
        page.candidates.emplace_back(entry->ordinal_,policy::ExecutionAccessInput{*summary,entry->targets_});
      }
      if(before&&before<=upper&&candidate(before))page.next_scan=contracts::KeysetPosition{host_,upper,before};
      return page;
    } catch(...) {return failure();}
  }
  contracts::Result<void> complete_read(const std::shared_ptr<Entry>& entry,
      std::span<const contracts::FactSummary> facts,contracts::EvidenceState evidence,
      std::optional<contracts::Error> fault) {
    std::lock_guard lock(mutex_);
    if(!contains(entry)||entry->summary_.phase==contracts::ExecutionPhase::Terminal||
       facts.size()>8||evidence==contracts::EvidenceState::RequiredRecordFailed)return failure();
    auto version=entry->summary_.version.next();if(!version)return contracts::make_unexpected(version.error());
    // facts 槽已在 prepare 预留；可靠完成路径不构造新的 shared summary。
    entry->summary_.facts.assign(facts.begin(),facts.end());
    entry->summary_.evidence=evidence;
    if(fault)entry->summary_.fault=contracts::Error{fault->code()};
    entry->summary_.version=*version;
    return {};
  }
  // 普通缓存回收不摘除被外部 owner 持有的记录；每轮扫描有显式上限。
  std::size_t trim(std::size_t scan_limit) {
    std::size_t removed=0,scanned=0;
    while(scanned<scan_limit) {
      std::shared_ptr<Entry> retired;
      {
        std::lock_guard lock(mutex_);
        if(terminal_count_<=ledger_->limits.terminal_records&&terminal_bytes_<=ledger_->limits.terminal_bytes)break;
        auto i=ordered_.upper_bound(trim_after_);
        if(i==ordered_.end()){trim_after_=0;break;}
        trim_after_=i->first;++scanned;
        // 三个索引分别持有一次，其他引用均视为 pin。
        if(i->second.use_count()!=3||!i->second->accepted_||i->second->summary_.phase!=contracts::ExecutionPhase::Terminal)continue;
        retired=std::move(i->second);by_id_.erase(retired->execution().execution_id);ordered_.erase(i);++removed;
        terminal_owners_.erase(owner_key(*retired));
        --terminal_count_;terminal_bytes_-=retired->reservation_->reply;
      }
      retired.reset();
    }
    return removed;
  }
private:
  static foundation::Unexpected<contracts::Error> failure() {return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));}
  using OwnerKey=std::pair<contracts::PrincipalId,std::uint64_t>;
  using OwnerIndex=std::map<OwnerKey,std::shared_ptr<Entry>>;
  static OwnerKey owner_key(const Entry& entry) {return {entry.summary_.owner.principal_id,entry.ordinal_};}
  static std::shared_ptr<Entry> before_owner(const OwnerIndex& index,contracts::PrincipalId owner,std::uint64_t before) {
    auto i=index.lower_bound({owner,before});if(i==index.begin())return {};
    --i;return i->first.first==owner?i->second:nullptr;
  }
  explicit ExecutionTable(Limits limits,contracts::HostIncarnation host):ledger_(std::make_shared<Ledger>()),host_(host) {ledger_->limits=limits;}
  bool contains(const std::shared_ptr<Entry>& entry) const {
    if(!entry)return false;auto i=by_id_.find(entry->execution().execution_id);
    return i!=by_id_.end()&&i->second==entry;
  }
  std::shared_ptr<Ledger> ledger_;
  const contracts::HostIncarnation host_;
  mutable std::mutex mutex_;
  std::map<foundation::TaskId,std::shared_ptr<Entry>> by_id_;
  std::map<std::uint64_t,std::shared_ptr<Entry>> ordered_;
  OwnerIndex active_owners_,terminal_owners_;
  std::uint64_t next_=0,trim_after_=0;
  std::size_t terminal_count_=0,terminal_bytes_=0;
  bool closed_=false;
};
class ExecutionSource final : public policy::ExecutionAccessSourcePort {
public:
  explicit ExecutionSource(std::shared_ptr<ExecutionTable> table):table_(std::move(table)) {
    foundation::invariant(bool(table_));
  }
  policy::ObservationSourceIdentity identity() const noexcept override {return {table_->host(),policy::RestoreMode::Absent};}
  contracts::Result<policy::ExecutionAccessInput> find(contracts::ExecutionRef ref) override {return table_->access_find(ref);}
  contracts::Result<policy::AccessScanPage> scan(const policy::AccessScanRequest& request) override {return table_->access_scan(request.list);}
private:
  std::shared_ptr<ExecutionTable> table_;
};
}
