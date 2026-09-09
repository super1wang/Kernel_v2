#include <ock/runtime/scheduler.hpp>
#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <set>
namespace ock::runtime::scheduler {
using foundation::make_unexpected;
struct Scheduler::State {
  enum class Submission { Waiting, Pending, Accepted, Rejected };
  struct Entry;
  struct Queue {Entry *first=nullptr,*last=nullptr;};
  struct SubjectState {
    Subject config;
    std::size_t queued=0,inflight=0;
    std::array<Queue,8> ready;
    unsigned priority=7,credit=8;
  };
  struct Entry {
    Ticket id;
    Request request;
    std::size_t subject,remaining=0;
    std::set<Ticket> children;
    std::uint64_t generation=1;
    bool ownership_returned=false;
    Submission submission=Submission::Waiting;
    bool linked=false,started=false,terminal=false,has_deadline=false;
    Entry *previous=nullptr,*next=nullptr,*completed_next=nullptr,*history_next=nullptr;
    std::optional<Result<void>> outcome;
    std::multimap<Time,Ticket>::iterator deadline;
    Entry(Ticket i,Request r,std::size_t s):id(i),request(std::move(r)),subject(s){}
  };
  std::weak_ptr<contracts::ExecutorPort> executor;
  std::vector<SubjectState> subjects;
  Options options;
  std::function<void()> wake;
  mutable std::mutex mutex;
  std::map<Ticket,std::shared_ptr<Entry>> active,history;
  std::multimap<Time,Ticket> deadlines;
  std::deque<std::function<void()>> controls;
  Entry *completed_first=nullptr,*completed_last=nullptr,*history_first=nullptr,*history_last=nullptr;
  Ticket next_id=1;
  std::size_t queued=0,inflight=0,delivery=0,edges=0,subject_cursor=0;
  unsigned subject_credit=0;
  bool closing=false,pumping=false,notifying=false;
  std::uint64_t dispatched=0,inspections=0,violations=0,callback_errors=0;
  void link(Entry &e) noexcept {
    if(e.linked || e.terminal || e.remaining || !e.request.resource_ready || e.submission!=Submission::Waiting)return;
    auto &q=subjects[e.subject].ready[e.request.priority];e.previous=q.last;e.next=nullptr;
    if(q.last)q.last->next=&e;else q.first=&e;q.last=&e;e.linked=true;
  }
  void unlink(Entry &e) noexcept {
    if(!e.linked)return;
    auto &q=subjects[e.subject].ready[e.request.priority];
    if(e.previous)e.previous->next=e.next;else q.first=e.next;
    if(e.next)e.next->previous=e.previous;else q.last=e.previous;
    e.previous=e.next=nullptr;e.linked=false;
  }
  void remove_deadline(Entry &e) noexcept {if(e.has_deadline){deadlines.erase(e.deadline);e.has_deadline=false;}}
  void finish(Entry &e,Result<void> result) noexcept { // mutex held; notification queue also owns dependency propagation.
    if(e.terminal)return;
    unlink(e);remove_deadline(e);
    ++e.generation;
    for(auto parent:e.request.dependencies) {auto it=active.find(parent);if(it!=active.end())edges-=it->second->children.erase(e.id);}
    if(e.submission==Submission::Waiting){--queued;--subjects[e.subject].queued;}
    else{--inflight;--subjects[e.subject].inflight;}
    e.terminal=true;e.outcome.emplace(std::move(result));
    e.completed_next=nullptr;if(completed_last)completed_last->completed_next=&e;else completed_first=&e;completed_last=&e;
  }
  void notify() noexcept {
    {std::lock_guard lock(mutex);if(notifying)return;notifying=true;}
    bool changed=false;
    for(;;) {
      std::shared_ptr<Entry> e;
      contracts::CallbackWork::Completion done;
      Request released_request;
      std::function<void()> detach;
      {
        std::lock_guard lock(mutex);
        if(!completed_first){notifying=false;break;}
        auto *raw=completed_first;completed_first=raw->completed_next;if(!completed_first)completed_last=nullptr;
        changed=true;e=active.at(raw->id);
        while(!e->children.empty()) {
          auto child=*e->children.begin();e->children.erase(e->children.begin());--edges;
          auto it=active.find(child);if(it==active.end() || it->second->terminal)continue;
          auto &c=*it->second;
          if(!*e->outcome)finish(c,make_unexpected(error(Errc::DependencyFailed)));
          else{--c.remaining;link(c);}
        }
        done=std::move(e->request.completed);
        detach=std::move(e->request.detach_waiter);
        released_request=std::move(e->request);
        auto node=active.extract(e->id);history.insert(std::move(node));
        e->history_next=nullptr;if(history_last)history_last->history_next=e.get();else history_first=e.get();history_last=e.get();
        while(history.size()>options.history) {
          auto *old=history_first;history_first=old->history_next;if(!history_first)history_last=nullptr;
          history.erase(old->id);
        }
      }
      try {if(detach)detach();}catch(...){std::lock_guard lock(mutex);++callback_errors;}
      try {if(done)done(*e->outcome);}catch(...){std::lock_guard lock(mutex);++callback_errors;}
      // callbacks/captures 析构与外部通知始终在锁外。
    }
    if(changed)signal();
  }
  void signal() noexcept {try{if(wake)wake();}catch(...){std::lock_guard lock(mutex);++callback_errors;}}
  struct Work final : contracts::ReadyWork {
    std::shared_ptr<State> owner;
    std::shared_ptr<Entry> entry;
    std::uint64_t generation;
    Work(std::shared_ptr<State> s,std::shared_ptr<Entry> e,std::uint64_t g):owner(std::move(s)),entry(std::move(e)),generation(g){}
    ~Work() override {
      {std::lock_guard lock(owner->mutex);--owner->delivery;entry->ownership_returned=true;
        if(!entry->terminal && !entry->started && entry->submission==Submission::Accepted) {
          ++owner->violations;owner->finish(*entry,make_unexpected(error(Errc::ExecutorViolation)));
        }}
      owner->notify();owner->signal();
    }
    void execute() noexcept override {
      auto &s=*owner;auto &e=*entry;
      bool run=false;
      {
        std::lock_guard lock(s.mutex);
        if(e.generation!=generation || e.started || e.terminal || e.submission==Submission::Rejected){++s.violations;return;}
        // 同一 mutex 内与 expiration 仲裁；仅本处成功 claim 才表示 Started。
        if(std::chrono::steady_clock::now()>=e.request.deadline)s.finish(e,make_unexpected(error(Errc::ExpiredBeforeDispatch)));
        else {e.started=true;s.remove_deadline(e);run=true;}
      }
      if(!run){s.notify();return;}
      Result<void> result;
      try {result=e.request.work();}catch(...){result=make_unexpected(error(Errc::WorkException));}
      {
        std::lock_guard lock(s.mutex);
        // 合法 inline 在 submit 返回前完成；后续拒绝只记违约，不能回滚事实。
        s.finish(e,std::move(result));
      }
      s.notify();
    }
  };
  Entry *choose() noexcept {
    for(std::size_t count=0;count<subjects.size();++count) {
      ++inspections;auto &s=subjects[subject_cursor];
      if(!subject_credit)subject_credit=s.config.weight;
      if(s.inflight<s.config.max_inflight) {
        for(unsigned p=0;p<8;++p) {
          auto &q=s.ready[s.priority];
          if(q.first) {
            auto *e=q.first;unlink(*e);
            if(!--s.credit){s.priority=(s.priority+7)%8;s.credit=s.priority+1;}
            if(!--subject_credit)subject_cursor=(subject_cursor+1)%subjects.size();
            return e;
          }
          s.priority=(s.priority+7)%8;s.credit=s.priority+1;
        }
      }
      subject_cursor=(subject_cursor+1)%subjects.size();subject_credit=0;
    }
    return nullptr;
  }
};
Result<std::unique_ptr<Scheduler>> Scheduler::create(std::shared_ptr<contracts::ExecutorPort> executor,std::vector<Subject> subjects,Options options,std::function<void()> wake) {
  if(!executor || subjects.empty() || subjects.size()>256 || !options.global_queued || options.global_queued>4096 ||
      !options.subject_queued || options.subject_queued>256 || !options.max_inflight || options.max_inflight>256 ||
      options.history>10000 || !options.max_dependencies || options.max_dependencies>64 || !options.control_slots || options.control_slots>128 || !options.worker_delivery || options.worker_delivery>4096)
    return make_unexpected(error(Errc::InvalidInput));
  for(std::size_t i=0;i<subjects.size();++i) {
    if(subjects[i].principal.empty() || !subjects[i].weight || subjects[i].weight>16 || !subjects[i].max_inflight || subjects[i].max_inflight>options.max_inflight)
      return make_unexpected(error(Errc::InvalidInput));
    for(std::size_t j=0;j<i;++j)if(subjects[i].principal==subjects[j].principal)return make_unexpected(error(Errc::InvalidInput));
  }
  try {
    auto s=std::make_shared<State>();s->executor=executor;s->options=options;s->wake=std::move(wake);
    for(auto subject:subjects)s->subjects.push_back(State::SubjectState{subject});
    return std::unique_ptr<Scheduler>(new Scheduler(std::move(s)));
  }catch(...){return make_unexpected(error(Errc::Full));}
}
Scheduler::~Scheduler(){close();}
Result<Ticket> Scheduler::enqueue(Request request) {
  auto s=state_;std::shared_ptr<State::Entry> e;
  std::unique_lock lock(s->mutex);
  if(s->closing)return make_unexpected(error(Errc::Closed));
  if(!request.work || !request.completed || request.priority>7 || request.dependencies.size()>s->options.max_dependencies || !s->next_id)
    return make_unexpected(error(Errc::InvalidInput));
  std::size_t subject=0;while(subject<s->subjects.size() && s->subjects[subject].config.principal!=request.principal)++subject;
  if(subject==s->subjects.size())return make_unexpected(error(Errc::InvalidInput));
  // 完成回调阻塞时，待通知终态也占用 entry 容量，不能绕过队列预算积累 owner。
  if(s->active.size()>=s->options.global_queued+s->options.max_inflight || s->queued>=s->options.global_queued || s->subjects[subject].queued>=s->options.subject_queued)return make_unexpected(error(Errc::Full));
  for(std::size_t i=0;i<request.dependencies.size();++i) {
    auto id=request.dependencies[i];
    if(id==s->next_id)return make_unexpected(error(Errc::InvalidInput));
    for(std::size_t j=0;j<i;++j)if(request.dependencies[j]==id)return make_unexpected(error(Errc::InvalidInput));
    auto a=s->active.find(id),h=s->history.find(id);
    if(a==s->active.end() && h==s->history.end())return make_unexpected(error(Errc::UnknownTicket));
    auto predecessor=a!=s->active.end()?a->second:h->second;
    if(predecessor->terminal && !*predecessor->outcome)return make_unexpected(error(Errc::DependencyFailed));
  }
  try {
    e=std::make_shared<State::Entry>(s->next_id,std::move(request),subject);
    for(auto id:e->request.dependencies) {
      auto it=s->active.find(id);if(it!=s->active.end() && !it->second->terminal) {
        it->second->children.insert(e->id);++s->edges;++e->remaining;
      }
    }
    s->active.emplace(e->id,e);
    if(e->request.deadline!=Time::max()){e->deadline=s->deadlines.emplace(e->request.deadline,e->id);e->has_deadline=true;}
  }catch(...) {
    if(e) {for(auto id:e->request.dependencies){auto it=s->active.find(id);if(it!=s->active.end())s->edges-=it->second->children.erase(e->id);}s->active.erase(e->id);s->remove_deadline(*e);}
    lock.unlock();return make_unexpected(error(Errc::Full));
  }
  ++s->next_id;++s->queued;++s->subjects[subject].queued;s->link(*e);
  lock.unlock();s->signal();return e->id;
}
Result<void> Scheduler::make_ready(Ticket id) {
  auto s=state_;
  {std::lock_guard lock(s->mutex);auto it=s->active.find(id);
    if(it==s->active.end() || it->second->terminal || it->second->submission!=State::Submission::Waiting)return make_unexpected(error(Errc::UnknownTicket));
    it->second->request.resource_ready=true;s->link(*it->second);}
  s->signal();return {};
}
Result<void> Scheduler::post_control(std::function<void()> command) {
  auto s=state_;
  {std::lock_guard lock(s->mutex);if(!command)return make_unexpected(error(Errc::InvalidInput));if(s->closing)return make_unexpected(error(Errc::Closed));
    if(s->controls.size()==s->options.control_slots)return make_unexpected(error(Errc::Full));
    try{s->controls.push_back(std::move(command));}catch(...){return make_unexpected(error(Errc::Full));}}
  s->signal();return {};
}
std::size_t Scheduler::pump(Time now,std::size_t limit) {
  auto s=state_;
  {std::lock_guard lock(s->mutex);if(s->pumping)return 0;s->pumping=true;}
  struct Reset{std::shared_ptr<State>s;~Reset(){std::lock_guard lock(s->mutex);s->pumping=false;}}reset{s};
  for(std::size_t i=0;i<s->options.control_slots;++i) {
    std::function<void()> command;
    {std::lock_guard lock(s->mutex);if(s->controls.empty())break;command=std::move(s->controls.front());s->controls.pop_front();}
    try{command();}catch(...){std::lock_guard lock(s->mutex);++s->callback_errors;}
  }
  {std::lock_guard lock(s->mutex);while(!s->deadlines.empty() && s->deadlines.begin()->first<=now) {
    auto it=s->active.find(s->deadlines.begin()->second);s->finish(*it->second,make_unexpected(error(Errc::ExpiredBeforeDispatch)));}}
  s->notify();
  std::size_t count=0;
  while(count<limit) {
    std::shared_ptr<State::Entry> e;std::uint64_t attempt=0;
    {std::lock_guard lock(s->mutex);if(s->closing || s->inflight>=s->options.max_inflight || s->delivery>=s->options.worker_delivery)break;
      auto *raw=s->choose();if(!raw)break;e=s->active.at(raw->id);attempt=e->generation;
      e->submission=State::Submission::Pending;--s->queued;--s->subjects[e->subject].queued;++s->inflight;++s->subjects[e->subject].inflight;++s->dispatched;++s->delivery;}
    Result<void> submitted;
    bool created=false;
    try{auto executor=s->executor.lock();if(!executor)submitted=make_unexpected(error(Errc::ExecutorRejected));
      else {auto work=std::make_unique<State::Work>(s,e,attempt);created=true;submitted=executor->submit(std::move(work));}}
    catch(...){submitted=make_unexpected(error(Errc::ExecutorRejected));}
    if(!created){std::lock_guard lock(s->mutex);--s->delivery;e->ownership_returned=true;}
    {
      std::lock_guard lock(s->mutex);
      if(!submitted && e->started)++s->violations;
      if(!e->terminal) {
        e->submission=submitted?State::Submission::Accepted:State::Submission::Rejected;
        if(!submitted && !e->started)s->finish(*e,make_unexpected(error(Errc::ExecutorRejected)));
        else if(submitted && e->ownership_returned && !e->started) {
          ++s->violations;s->finish(*e,make_unexpected(error(Errc::ExecutorViolation)));
        }
      }
    }
    s->notify();++count;
  }
  return count;
}
std::optional<Time> Scheduler::next_deadline()const{std::lock_guard lock(state_->mutex);if(state_->deadlines.empty())return {};return state_->deadlines.begin()->first;}
Snapshot Scheduler::snapshot()const {
  std::lock_guard lock(state_->mutex);auto &s=*state_;
  return {s.active.size(),s.queued,s.inflight,s.history.size(),s.controls.size(),s.delivery,s.edges,s.dispatched,s.inspections,s.violations,s.callback_errors};
}
void Scheduler::close(){auto s=state_;std::deque<std::function<void()>> discarded;{std::lock_guard lock(s->mutex);s->closing=true;discarded.swap(s->controls);for(auto &[id,e]:s->active)if(e->submission==State::Submission::Waiting && !e->terminal)s->finish(*e,make_unexpected(error(Errc::Closed)));}s->notify();}
}
