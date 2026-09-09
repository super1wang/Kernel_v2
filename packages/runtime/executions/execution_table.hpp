#pragma once
#include <ock/contracts/observation.hpp>
#include <map>
#include <mutex>

namespace ock::runtime::executions::detail {
// 内部拥有表。调用者身份与观察权限由 Runtime 适配器检查，不能直接安装为 SDK 入口。
class ExecutionTable final {
public:
  struct Limits {
    std::size_t records=4096, input_bytes=64*1024*1024, reply_bytes=64*1024*1024;
    std::size_t terminal_records=10000, terminal_bytes=64*1024*1024;
  };
  struct Usage {std::size_t records=0,input_bytes=0,reply_bytes=0;};
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
        contracts::CppTypeToken type,std::shared_ptr<Reservation> reservation)
        :reservation_(std::move(reservation)),payload_(std::move(payload)),
         result_type_(type),summary_(std::move(summary)) {summary_.facts.reserve(8);}
    // reservation 最后析构，确保 payload 析构及 capture 释放期间仍计费。
    std::shared_ptr<Reservation> reservation_;
    std::shared_ptr<void> payload_;
    contracts::CppTypeToken result_type_;
    contracts::SummaryInput summary_;
    std::uint64_t ordinal_=0;
    bool accepted_=false;
  };
  static contracts::Result<std::shared_ptr<ExecutionTable>> create(Limits limits) {
    if(!limits.records||!limits.input_bytes||!limits.reply_bytes||
       !limits.terminal_records||!limits.terminal_bytes)return failure();
    try {return std::shared_ptr<ExecutionTable>(new ExecutionTable(limits));}
    catch(...) {return failure();}
  }
  // summary/identity 必须由可信适配器生成；此处仍验证格式与重复身份。
  // accepted=false 的内部条目不可观察；安装失败从未发布 Accepted。
  contracts::Result<std::shared_ptr<Entry>> prepare(contracts::SummaryInput summary,
      std::shared_ptr<void> payload,contracts::CppTypeToken type,
      std::size_t input_bytes,std::size_t reply_bytes) {
    if(!payload||summary.phase!=contracts::ExecutionPhase::Queued||
       summary.version.value()!=1||!summary.facts.empty())return failure();
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
      auto entry=std::shared_ptr<Entry>(new Entry(std::move(summary),std::move(payload),type,std::move(reservation)));
      std::lock_guard lock(mutex_);
      if(closed_||next_==(std::numeric_limits<std::uint64_t>::max)()||
         by_id_.contains(entry->execution().execution_id))return failure();
      entry->ordinal_=++next_; // 拒绝也不复用 ordinal。
      auto inserted=ordered_.emplace(entry->ordinal_,entry);
      try {by_id_.emplace(entry->execution().execution_id,entry);}
      catch(...) {ordered_.erase(inserted.first);throw;}
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
    by_id_.erase(entry->execution().execution_id);ordered_.erase(entry->ordinal_);return true;
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
    if(phase==contracts::ExecutionPhase::Terminal&&entry->accepted_) {
      ++terminal_count_;terminal_bytes_+=entry->reservation_->reply;
    }
    if(fault)entry->summary_.fault=contracts::Error{fault->code()};
    return {};
  }
  Usage usage() const {std::lock_guard lock(ledger_->mutex);return ledger_->used;}
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
        // 两个索引分别持有一次，其他引用均视为 pin。
        if(i->second.use_count()!=2||!i->second->accepted_||i->second->summary_.phase!=contracts::ExecutionPhase::Terminal)continue;
        retired=std::move(i->second);by_id_.erase(retired->execution().execution_id);ordered_.erase(i);++removed;
        --terminal_count_;terminal_bytes_-=retired->reservation_->reply;
      }
      retired.reset();
    }
    return removed;
  }
private:
  static foundation::Unexpected<contracts::Error> failure() {return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));}
  explicit ExecutionTable(Limits limits):ledger_(std::make_shared<Ledger>()) {ledger_->limits=limits;}
  bool contains(const std::shared_ptr<Entry>& entry) const {
    if(!entry)return false;auto i=by_id_.find(entry->execution().execution_id);
    return i!=by_id_.end()&&i->second==entry;
  }
  std::shared_ptr<Ledger> ledger_;
  mutable std::mutex mutex_;
  std::map<foundation::TaskId,std::shared_ptr<Entry>> by_id_;
  std::map<std::uint64_t,std::shared_ptr<Entry>> ordered_;
  std::uint64_t next_=0,trim_after_=0;
  std::size_t terminal_count_=0,terminal_bytes_=0;
  bool closed_=false;
};
}
