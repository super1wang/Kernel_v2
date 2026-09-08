#include <ock/runtime/logging.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <type_traits>

namespace ock::runtime::observability {
namespace {
using namespace contracts;
using foundation::make_unexpected;
using foundation::Result;
constexpr auto max_count=std::numeric_limits<std::uint64_t>::max();

// 每次只饱和单个诊断计数；水位本身从不回绕或复用。
void increment(std::atomic<std::uint64_t>& value,std::atomic<bool>& saturated) noexcept {
  auto old=value.load(std::memory_order_relaxed);
  for(;;){
    if(old==max_count){saturated.store(true,std::memory_order_relaxed);return;}
    if(value.compare_exchange_weak(old,old+1,std::memory_order_relaxed))return;
  }
}
#define OCK_LOG_COUNTERS(X) \
 X(accepted) X(rejected) X(dropped_before_accept) X(evicted_after_accept) \
 X(rejected_invalid) X(rejected_full) X(rejected_busy) X(rejected_closed) \
 X(rejected_backend) X(rejected_sequence) X(dropped_filtered) \
 X(snapshot_failures) X(flush_failures) X(close_failures)
#define OCK_FACADE_COUNTERS(X) \
 X(write_attempts) X(accepted) X(rejected) X(dropped) \
 X(write_backend_exceptions) X(write_protocol_failures) X(write_reentrant) X(acceptance_unknown) \
 X(snapshot_attempts) X(snapshot_failures) X(snapshot_backend_exceptions) X(snapshot_protocol_failures) X(snapshot_reentrant) \
 X(flush_attempts) X(flush_failures) X(flush_backend_exceptions) X(flush_protocol_failures) X(flush_reentrant) \
 X(close_attempts) X(close_failures) X(close_backend_exceptions) X(close_protocol_failures) X(close_reentrant)
#define DECLARE_COUNTER(name) std::atomic<std::uint64_t> name{};
#define READ_COUNTER(name) result.name=name.load(std::memory_order_relaxed);
struct BackendCounts {
  OCK_LOG_COUNTERS(DECLARE_COUNTER)
  std::atomic<bool> saturated{};
  LogCounters read() const noexcept { LogCounters result{}; OCK_LOG_COUNTERS(READ_COUNTER) result.saturated=saturated.load(std::memory_order_relaxed);return result; }
  void add(std::atomic<std::uint64_t>& field) noexcept { increment(field,saturated); }
};
struct FacadeCounts {
  OCK_FACADE_COUNTERS(DECLARE_COUNTER)
  std::atomic<bool> saturated{};
  FacadeCounters read() const noexcept { FacadeCounters result{}; OCK_FACADE_COUNTERS(READ_COUNTER) result.saturated=saturated.load(std::memory_order_relaxed);return result; }
  void add(std::atomic<std::uint64_t>& field) noexcept { increment(field,saturated); }
};
#undef DECLARE_COUNTER
#undef READ_COUNTER
#undef OCK_LOG_COUNTERS
#undef OCK_FACADE_COUNTERS

template<class E> constexpr unsigned number(E e) noexcept { return static_cast<unsigned>(e); }
bool valid_limits(LogLimits limits) noexcept {
  return limits.record_capacity>=1&&limits.record_capacity<=4096&&number(limits.full)<=1&&number(limits.minimum_level)<=4;
}
bool same_limits(LogLimits a,LogLimits b) noexcept {
  return a.record_capacity==b.record_capacity&&a.full==b.full&&a.minimum_level==b.minimum_level;
}
bool same_stream(LogPosition a,LogPosition b) noexcept { return a.host==b.host&&a.stream==b.stream; }
bool valid_snapshot(const LogSnapshot& s) noexcept {
  return !s.accepted_through.host.empty()&&s.accepted_through.stream!=0&&valid_limits(s.limits)&&number(s.state)<=2&&
    s.evicted_through<=s.accepted_through.accepted_sequence&&
    s.retained_count==s.accepted_through.accepted_sequence-s.evicted_through&&s.retained_count<=s.limits.record_capacity&&
    s.counters.accepted==s.accepted_through.accepted_sequence&&s.counters.evicted_after_accept==s.evicted_through;
}
bool valid_error(const foundation::Error& e) noexcept {
  return &e.code().domain()==&logging_domain&&e.code().value()>=1&&e.code().value()<=11&&!e.info();
}
LogWriteResult reject(LogReason reason) noexcept { return {LogDecision::Rejected,reason,{}}; }
bool valid_write(const LogWriteResult& r,LogPosition stream) noexcept {
  switch(r.decision){
  case LogDecision::Accepted:return r.reason==LogReason::None&&r.accepted&&same_stream(*r.accepted,stream)&&r.accepted->accepted_sequence!=0;
  case LogDecision::Dropped:return r.reason==LogReason::Filtered&&!r.accepted;
  case LogDecision::Rejected:return !r.accepted&&number(r.reason)>=number(LogReason::InvalidRecord)&&number(r.reason)<=number(LogReason::SequenceExhausted);
  default:return false;
  }
}
bool valid_flush(const LogFlushResult& r,LogPosition stream) noexcept {
  return same_stream(r.covered_through,stream)&&r.evicted_through<=r.covered_through.accepted_sequence&&r.volatile_only;
}

// 只复制静态公共格式；字段按指针索引，Redacted 的 value 不读取。
bool format(const LogInput& in,PublicLogRecord& record) noexcept {
  // 对静态表取保守上界：最长标签、四个键及 uint64 十进制仍含尾零。
  static_assert(15+7+11+10+7+14+4*(1+6+1+6+20)<256);
  constexpr std::string_view levels[]{"trace","debug","info","warning","error"};
  constexpr std::string_view components[]{"host","registry","policy","invocation"};
  constexpr std::string_view events[]{"","configured","starting","module_started","ready","start_failed","stop_accepting","module_stopped","log_closing","diagnostic"};
  constexpr std::string_view keys[]{"","status","count","module","detail"};
  const auto l=number(in.level),c=number(in.component),e=number(in.event);
  if(l>4||c>3||e<1||e>9||in.fields.size()>4)return false;
  if(e!=9&&(c!=0||l!=(e==5?4u:2u)))return false;
  std::array<const LogField*,5> fields{};
  for(const auto& field:in.fields){
    auto k=number(field.key),v=number(field.visibility);
    if(k<1||k>4||v>2||fields[k])return false;
    fields[k]=&field;
    if(v==0)continue;
    if(k==4||(k==2?v!=2:v!=1))return false;
    if((k==1||k==3)&&field.value>65535)return false;
    if(k==3&&field.value==0)return false;
  }
  std::size_t used=0;
  auto append=[&](std::string_view value) noexcept {
    if(value.size()>=record.text.size()-used)return false;
    std::copy(value.begin(),value.end(),record.text.begin()+used);used+=value.size();return true;
  };
  if(!append("ock.log/1 level=")||!append(levels[l])||!append(" component=")||!append(components[c])||!append(" event=")||!append(events[e]))return false;
  for(unsigned k=1;k<=4;++k)if(auto field=fields[k]){
    if(!append(" ")||!append(keys[k])||!append("="))return false;
    if(field->visibility==LogValueClass::Redacted){if(!append("<redacted>"))return false;}
    else{
      if(!append(field->visibility==LogValueClass::PublicCode?"code:":"count:"))return false;
      std::array<char,20> digits{};auto converted=std::to_chars(digits.data(),digits.data()+digits.size(),field->value);
      if(converted.ec!=std::errc{}||!append({digits.data(),static_cast<std::size_t>(converted.ptr-digits.data())}))return false;
    }
  }
  record.level=in.level;record.text_size=static_cast<std::uint16_t>(used);return true;
}

class MemoryLogging final : public LogPort,public MemoryDiagnostics {
public:
  MemoryLogging(HostIncarnation host,std::uint64_t stream,LogLimits limits)
    :host_(host),stream_(stream),limits_(limits),records_(std::make_unique<PublicLogRecord[]>(limits.record_capacity)){}
  LogWriteResult try_write(const LogInput& in) override {
    PublicLogRecord record{};
    if(!format(in,record))return rejected(LogReason::InvalidRecord,counts_.rejected_invalid);
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return rejected(LogReason::Busy,counts_.rejected_busy);
    if(state_!=LogState::Open)return rejected(LogReason::Closed,counts_.rejected_closed);
    if(in.level<limits_.minimum_level){counts_.add(counts_.dropped_before_accept);counts_.add(counts_.dropped_filtered);return {LogDecision::Dropped,LogReason::Filtered,{}};}
    if(accepted_==max_count)return rejected(LogReason::SequenceExhausted,counts_.rejected_sequence);
    const auto full=accepted_-evicted_==limits_.record_capacity;
    if(full&&limits_.full==LogOverflow::RejectNewest)return rejected(LogReason::Full,counts_.rejected_full);
    record.position=position(++accepted_);
    records_[static_cast<std::size_t>((accepted_-1)%limits_.record_capacity)]=record;
    counts_.add(counts_.accepted);
    if(full){++evicted_;counts_.add(counts_.evicted_after_accept);}
    return {LogDecision::Accepted,LogReason::None,record.position};
  }
  Result<LogSnapshot> snapshot() override {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return failed<LogSnapshot>(LogErrc::Busy,counts_.snapshot_failures);
    return LogSnapshot{position(accepted_),evicted_,accepted_-evicted_,limits_,state_,counts_.read()};
  }
  Result<LogFlushResult> flush(LogPosition through) override {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return failed<LogFlushResult>(LogErrc::Busy,counts_.flush_failures);
    if(!valid_position(through))return failed<LogFlushResult>(LogErrc::InvalidPosition,counts_.flush_failures);
    return LogFlushResult{through,std::min(evicted_,through.accepted_sequence),true};
  }
  Result<LogFlushResult> close() override {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return failed<LogFlushResult>(LogErrc::Busy,counts_.close_failures);
    state_=LogState::Closed;
    return LogFlushResult{position(accepted_),evicted_,true};
  }
  Result<LogReadPage> copy_records(LogPosition after,std::span<PublicLogRecord> out) override {
    if(out.empty()||out.size()>64)return make_unexpected(log_error(LogErrc::InvalidReadPage));
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return make_unexpected(log_error(LogErrc::Busy));
    if(!valid_position(after))return make_unexpected(log_error(LogErrc::InvalidPosition));
    const auto base=std::max(after.accepted_sequence,evicted_);
    const auto count=std::min<std::uint64_t>(accepted_-base,out.size());
    for(std::uint64_t i=0;i<count;++i)out[static_cast<std::size_t>(i)]=records_[static_cast<std::size_t>((base+i)%limits_.record_capacity)];
    return LogReadPage{static_cast<std::uint32_t>(count),position(base+count),position(accepted_),evicted_,evicted_>after.accepted_sequence};
  }
private:
  LogPosition position(std::uint64_t seq) const noexcept { return {host_,stream_,seq}; }
  bool valid_position(LogPosition p) const noexcept { return same_stream(p,position(0))&&p.accepted_sequence<=accepted_; }
  LogWriteResult rejected(LogReason reason,std::atomic<std::uint64_t>& count) noexcept {
    counts_.add(counts_.rejected);counts_.add(count);return reject(reason);
  }
  template<class T> Result<T> failed(LogErrc reason,std::atomic<std::uint64_t>& count) noexcept {
    counts_.add(count);return make_unexpected(log_error(reason));
  }
  const HostIncarnation host_;
  const std::uint64_t stream_;
  const LogLimits limits_;
  std::unique_ptr<PublicLogRecord[]> records_;
  std::mutex mutex_;
  LogState state_{LogState::Open};
  std::uint64_t accepted_{},evicted_{};
  BackendCounts counts_;
};

// 同步虚调用的跨方法重入保护；identity 对应 LogPort 子对象且由局部 owner 保活。
struct CallFrame;
thread_local CallFrame* current_frame{};
struct CallFrame {
  LogPort* identity;
  CallFrame* previous;
  bool entered{};
  explicit CallFrame(LogPort* port) noexcept:identity(port),previous(current_frame) {
    unsigned depth=0;
    for(auto frame=previous;frame;frame=frame->previous){if(frame->identity==port||++depth>=16)return;}
    current_frame=this;entered=true;
  }
  ~CallFrame(){if(entered)current_frame=previous;}
  CallFrame(const CallFrame&)=delete;
};
enum class Control { Snapshot,Flush,Close };
struct ControlCounters {
  std::atomic<std::uint64_t> &attempts,&failures,&exceptions,&protocol,&reentrant;
};
ControlCounters control_counts(FacadeCounts& c,Control method) noexcept {
  if(method==Control::Snapshot)return {c.snapshot_attempts,c.snapshot_failures,c.snapshot_backend_exceptions,c.snapshot_protocol_failures,c.snapshot_reentrant};
  if(method==Control::Flush)return {c.flush_attempts,c.flush_failures,c.flush_backend_exceptions,c.flush_protocol_failures,c.flush_reentrant};
  return {c.close_attempts,c.close_failures,c.close_backend_exceptions,c.close_protocol_failures,c.close_reentrant};
}
template<class T,class State,class Invoke,class Validate>
Result<T> invoke_control(const std::shared_ptr<State>& owner,Control method,Invoke invoke,Validate valid) noexcept {
  if(!owner)return make_unexpected(log_error(LogErrc::InvalidLogger));
  auto& counts=owner->counts;auto metrics=control_counts(counts,method);counts.add(metrics.attempts);
  auto fail=[&](LogErrc error)->Result<T>{counts.add(metrics.failures);return make_unexpected(log_error(error));};
  CallFrame frame(owner->backend.get());
  if(!frame.entered){counts.add(metrics.reentrant);return fail(LogErrc::Reentrant);}
  try{
    auto result=invoke(*owner->backend);
    if(!result){
      if(!valid_error(result.error())){counts.add(metrics.protocol);return fail(LogErrc::BackendProtocolFailure);}
      counts.add(metrics.failures);return make_unexpected(result.error());
    }
    if(!valid(*result)){counts.add(metrics.protocol);return fail(LogErrc::BackendProtocolFailure);}
    return *result;
  }catch(...){counts.add(metrics.exceptions);return fail(LogErrc::BackendFailure);}
}
// 无抛元素及实际 Result 移动保证 noexcept 路径不会在返回运输中触发 terminate。
static_assert(std::is_nothrow_copy_constructible_v<LogWriteResult>);
static_assert(std::is_nothrow_move_constructible_v<Result<LogSnapshot>>);
static_assert(std::is_nothrow_move_constructible_v<Result<LogFlushResult>>);
static_assert(std::is_nothrow_move_constructible_v<Result<FacadeCounters>>);
static_assert(std::is_nothrow_copy_constructible_v<foundation::Error>);
}

struct SafeLogger::State {
  std::shared_ptr<contracts::LogPort> backend;
  contracts::LogPosition stream;
  contracts::LogLimits limits;
  FacadeCounts counts;
  State(std::shared_ptr<contracts::LogPort> port,const contracts::LogSnapshot& snapshot) noexcept
    :backend(std::move(port)),stream(snapshot.accepted_through),limits(snapshot.limits){}
};
SafeLogger::SafeLogger(std::shared_ptr<State> state) noexcept:state_(std::move(state)){}
SafeLogger::SafeLogger(const SafeLogger&) noexcept=default;
SafeLogger& SafeLogger::operator=(const SafeLogger&) noexcept=default;
SafeLogger::SafeLogger(SafeLogger&&) noexcept=default;
SafeLogger& SafeLogger::operator=(SafeLogger&&) noexcept=default;
SafeLogger::~SafeLogger()=default;

foundation::Result<SafeLogger> SafeLogger::create(std::shared_ptr<contracts::LogPort> backend) {
  // get() 非空仍可能是空 control block 的借用别名，不能提供在途保活。
  if(!backend||backend.use_count()==0)return make_unexpected(log_error(LogErrc::InvalidLogger));
  CallFrame frame(backend.get());
  if(!frame.entered)return make_unexpected(log_error(LogErrc::Reentrant));
  Result<LogSnapshot> snapshot=make_unexpected(log_error(LogErrc::BackendFailure));
  try{snapshot=backend->snapshot();}
  catch(...){return make_unexpected(log_error(LogErrc::BackendFailure));}
  if(!snapshot){if(valid_error(snapshot.error()))return make_unexpected(snapshot.error());return make_unexpected(log_error(LogErrc::BackendProtocolFailure));}
  if(!valid_snapshot(*snapshot))return make_unexpected(log_error(LogErrc::BackendProtocolFailure));
  try{return SafeLogger(std::make_shared<State>(backend,*snapshot));}
  catch(const std::bad_alloc&){return make_unexpected(log_error(LogErrc::AllocationFailure));}
}
contracts::LogWriteResult SafeLogger::try_write(const contracts::LogInput& in) noexcept {
  auto owner=state_; // 此后不再访问外壳；回调可同步销毁 *this。
  if(!owner)return reject(LogReason::InvalidLogger);
  auto& counts=owner->counts;counts.add(counts.write_attempts);
  LogWriteResult result=reject(LogReason::BackendFailure);
  {
    CallFrame frame(owner->backend.get());
    if(!frame.entered){counts.add(counts.write_reentrant);result=reject(LogReason::Reentrant);}
    else{
      try{
        result=owner->backend->try_write(in);
        if(!valid_write(result,owner->stream)){counts.add(counts.write_protocol_failures);counts.add(counts.acceptance_unknown);result=reject(LogReason::BackendProtocolFailure);}
      }catch(...){counts.add(counts.write_backend_exceptions);result=reject(LogReason::BackendFailure);}
    }
  }
  if(result.decision==LogDecision::Accepted)counts.add(counts.accepted);
  else if(result.decision==LogDecision::Dropped)counts.add(counts.dropped);
  else counts.add(counts.rejected);
  return result;
}
foundation::Result<contracts::LogSnapshot> SafeLogger::snapshot() noexcept {
  auto owner=state_;
  return invoke_control<LogSnapshot>(owner,Control::Snapshot,[](LogPort& port){return port.snapshot();},
    [&](const LogSnapshot& result){return valid_snapshot(result)&&same_stream(result.accepted_through,owner->stream)&&same_limits(result.limits,owner->limits);});
}
foundation::Result<contracts::LogFlushResult> SafeLogger::flush(contracts::LogPosition through) noexcept {
  auto owner=state_;
  // 跨流直接拒绝，也计为本次 facade 控制失败；不凭初始水位判断未来游标。
  if(owner&&!same_stream(through,owner->stream)){
    owner->counts.add(owner->counts.flush_attempts);owner->counts.add(owner->counts.flush_failures);
    return make_unexpected(log_error(LogErrc::InvalidPosition));
  }
  return invoke_control<LogFlushResult>(owner,Control::Flush,[&](LogPort& port){return port.flush(through);},
    [&](const LogFlushResult& result){return valid_flush(result,owner->stream)&&result.covered_through==through;});
}
foundation::Result<contracts::LogFlushResult> SafeLogger::close() noexcept {
  auto owner=state_;
  return invoke_control<LogFlushResult>(owner,Control::Close,[](LogPort& port){return port.close();},
    [&](const LogFlushResult& result){return valid_flush(result,owner->stream);});
}
foundation::Result<FacadeCounters> SafeLogger::counters() const noexcept {
  auto owner=state_;if(!owner)return make_unexpected(log_error(LogErrc::InvalidLogger));return owner->counts.read();
}
foundation::Result<MemoryLoggingBundle> make_memory_logging(contracts::HostIncarnation host,std::uint64_t stream,contracts::LogLimits limits) {
  if(host.empty()||stream==0||limits.record_capacity==0||number(limits.full)>1||number(limits.minimum_level)>4)
    return make_unexpected(log_error(LogErrc::InvalidConfiguration));
  if(limits.record_capacity>4096||limits.record_capacity>(std::numeric_limits<std::size_t>::max()-sizeof(MemoryLogging))/sizeof(PublicLogRecord))
    return make_unexpected(log_error(LogErrc::BudgetExceeded));
  try{auto state=std::make_shared<MemoryLogging>(host,stream,limits);return MemoryLoggingBundle{state,state};}
  catch(const std::bad_alloc&){return make_unexpected(log_error(LogErrc::AllocationFailure));}
}
}
