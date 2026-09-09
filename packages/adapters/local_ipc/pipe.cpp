#include "native_security.hpp"
#include <asio.hpp>
#include <asio/windows/stream_handle.hpp>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ock::local_ipc {
namespace {
class NativeHandle {
public:
  explicit NativeHandle(HANDLE value) noexcept : value_(value) {}
  NativeHandle(NativeHandle &&other) noexcept : value_(other.release()) {}
  NativeHandle(const NativeHandle &) = delete;
  ~NativeHandle() { if(value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  HANDLE get() const noexcept { return value_; }
  HANDLE release() noexcept { return std::exchange(value_,INVALID_HANDLE_VALUE); }
private:
  HANDLE value_;
};
bool valid(const PipeOptions &o) {
  return o.frame_bytes >= 1024 && o.frame_bytes <= 64 * 1024 * 1024 &&
      o.notification_bytes >= 12 && o.notification_bytes <= o.frame_bytes &&
      o.control_queue_bytes >= o.frame_bytes && o.notification_queue_bytes >= o.notification_bytes &&
      o.connections > 0 && o.connections <= 128 && o.io_timeout.count() > 0;
}
void finish_thread(std::thread &thread) {
  if(!thread.joinable()) return;
  if(thread.get_id() == std::this_thread::get_id()) thread.detach();
  else thread.join();
}
}
struct Connection::State : std::enable_shared_from_this<Connection::State> {
  PipeOptions options;
  asio::io_context io;
  asio::windows::stream_handle pipe;
  asio::steady_timer timer;
  asio::steady_timer read_timer;
  asio::steady_timer tick_timer;
  asio::windows::object_handle wake;
  NativeHandle prefix_event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
  OVERLAPPED prefix{};
  std::byte prefix_byte{};
  bool prefix_pending = false, suffix_pending = false, quarantined = false;
  std::optional<PeerIdentity> identity;
  control::FrameDecoder decoder;
  std::array<std::byte, 4096> input{};
  std::mutex mutex;
  std::condition_variable drained;
  bool finished = false;
  std::deque<std::vector<std::byte>> control, notifications;
  std::size_t control_bytes = 0, notification_bytes = 0;
  std::vector<std::byte> writing;
  Queue writing_kind = Queue::Control;
  bool closed = false, started = false, write_active = false, write_wakeup = false, draining = false;
  Message message;
  Closed disconnected;
  Tick tick;
  std::thread thread;
  State(NativeHandle handle, PipeOptions o) : options(std::move(o)), pipe(io), timer(io), read_timer(io), tick_timer(io),
      wake(io,CreateEventW(nullptr,FALSE,FALSE,nullptr)),
      decoder([&] { data::Budget b; b.frame_bytes = options.frame_bytes - 12; return b; }()) {
    if(!prefix_event.get()) throw std::system_error(GetLastError(),std::system_category());
    prefix.hEvent = reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(prefix_event.get()) | 1);
    pipe.assign(handle.get());
    (void)handle.release();
  }
  ~State() { finish_thread(thread); }
  void stop() {
    Closed notify;
    {
      std::lock_guard lock(mutex);
      if(closed) return;
      closed = true;
      control.clear(); notifications.clear();
      notify = std::move(disconnected);
    }
    std::error_code ignored;
    timer.cancel(ignored); read_timer.cancel(ignored); tick_timer.cancel(ignored); wake.cancel(ignored);
    pipe.cancel(ignored);
    if(prefix_pending) {
      DWORD transferred = 0;
      // 已离开 Policy 仲裁；OVERLAPPED/buffer 必须存活至取消完成。
      GetOverlappedResult(pipe.native_handle(),&prefix,&transferred,TRUE);
      prefix_pending = false;
    }
    pipe.close(ignored);
    if(notify) { try { notify(); } catch(...) {} }
    message = {};
    tick = {};
  }
  void pulse() {
    {std::lock_guard lock(mutex);if(closed||quarantined||draining||!tick)return;}
    tick_timer.expires_after(std::chrono::milliseconds(10));
    tick_timer.async_wait([self=shared_from_this()](std::error_code ec) {
      if(ec)return;
      {std::lock_guard lock(self->mutex);if(self->closed||self->quarantined||self->draining)return;}
      try {self->tick();self->pulse();} catch(...) {self->stop();}
    });
  }
  void watch_wake() {
    { std::lock_guard lock(mutex); if(closed) return; }
    wake.async_wait([self=shared_from_this()](std::error_code ec) {
      if(ec) return;
      bool suffix = false, quarantine = false;
      { std::lock_guard lock(self->mutex);
        quarantine = self->quarantined;
        suffix = self->suffix_pending; self->suffix_pending = false;
        if(self->closed) return;
      }
      if(quarantine) { self->stop(); return; }
      if(suffix) self->write_suffix();
      else self->write_next();
      self->watch_wake();
    });
  }
  void write_suffix() {
    arm();
    asio::async_write(pipe,asio::buffer(writing.data()+1,writing.size()-1),
      [self=shared_from_this()](std::error_code ec, std::size_t n) {
        if(ec || n+1 != self->writing.size()) { self->stop(); return; }
        self->timer.cancel();
        { std::lock_guard lock(self->mutex);
          if(self->closed) return;
          auto &used = self->writing_kind == Queue::Control ? self->control_bytes : self->notification_bytes;
          used -= self->writing.size(); self->writing.clear(); self->write_active = false;
        }
        self->write_next();
      });
  }
  void arm() {
    timer.expires_after(options.io_timeout);
    timer.async_wait([self=shared_from_this()](std::error_code ec) { if(!ec) self->stop(); });
  }
  void arm_read() {
    read_timer.expires_after(options.io_timeout);
    read_timer.async_wait([self=shared_from_this()](std::error_code ec) { if(!ec) self->stop(); });
  }
  void read() {
    pipe.async_read_some(asio::buffer(input), [self=shared_from_this()](std::error_code ec, std::size_t n) {
      { std::lock_guard lock(self->mutex); if(self->closed || self->quarantined || self->draining) return; }
      if(ec || n == 0) { self->stop(); return; }
      if(!self->identity) {
        auto peer = NativeSecurity::client(self->pipe.native_handle());
        if(!peer || std::find(self->options.allowed_client_sids.begin(), self->options.allowed_client_sids.end(), peer->sid()) == self->options.allowed_client_sids.end()) {
          self->stop(); return;
        }
        { std::lock_guard lock(self->mutex); self->identity.emplace(std::move(*peer)); }
      }
      std::size_t offset = 0;
      while(offset < n) {
        auto decoded = self->decoder.consume(std::span(self->input).subspan(offset,n-offset));
        if(!decoded || decoded->consumed == 0) { self->stop(); return; }
        offset += decoded->consumed;
        if(decoded->message) {
          self->arm_read();
          try { self->message(std::move(*decoded->message)); }
          catch(...) { self->stop(); return; }
        }
        { std::lock_guard lock(self->mutex); if(self->closed || self->quarantined || self->draining) return; }
      }
      self->read();
    });
  }
  void write_next() {
    bool finish = false;
    {
      std::lock_guard lock(mutex);
      write_wakeup = false;
      if(closed || quarantined || write_active) return;
      auto &queue = !control.empty() ? control : notifications;
      if(queue.empty()) {
        if(!draining) return;
        finish = true;
      } else {
        writing_kind = !control.empty() ? Queue::Control : Queue::Notification;
        writing = std::move(queue.front()); queue.pop_front(); write_active = true;
      }
    }
    if(finish) { stop(); return; }
    arm();
    asio::async_write(pipe,asio::buffer(writing), [self=shared_from_this()](std::error_code ec, std::size_t n) {
      if(ec || n != self->writing.size()) { self->stop(); return; }
      self->timer.cancel();
      {
        std::lock_guard lock(self->mutex);
        if(self->closed) return;
        auto &bytes = self->writing_kind == Queue::Control ? self->control_bytes : self->notification_bytes;
        bytes -= self->writing.size(); self->writing.clear(); self->write_active = false;
      }
      self->write_next();
    });
  }
};
Connection::Connection(std::shared_ptr<State> state) : state_(std::move(state)) {}
Connection::~Connection() { close(); }
std::optional<PeerIdentity> Connection::peer() const { std::lock_guard lock(state_->mutex); return state_->identity; }
void Connection::start(Message message, Closed closed, Tick tick) {
  auto s = state_;
  { std::lock_guard lock(s->mutex);
    if(s->started || s->closed) return;
    s->started = true; s->message = std::move(message); s->disconnected = std::move(closed);s->tick=std::move(tick);
  }
  try {
    s->thread = std::thread([s] {
      try { s->arm_read(); s->watch_wake(); s->read(); s->write_next(); s->pulse(); s->io.run(); }
      catch(...) { s->stop(); }
      std::lock_guard lock(s->mutex); s->finished = true; s->drained.notify_all();
    });
  } catch(...) {
    s->stop();
    std::lock_guard lock(s->mutex); s->finished = true; s->drained.notify_all();
  }
}
foundation::Result<void> Connection::send(std::span<const std::byte> bytes, Queue kind) {
  auto s = state_;
  bool wake = false;
  {
    std::lock_guard lock(s->mutex);
    if(s->closed || s->quarantined || s->draining) return foundation::make_unexpected(error(IpcErrc::Closed));
    auto &used = kind == Queue::Control ? s->control_bytes : s->notification_bytes;
    const auto limit = kind == Queue::Control ? s->options.control_queue_bytes : s->options.notification_queue_bytes;
    const auto frame = kind == Queue::Control ? s->options.frame_bytes : s->options.notification_bytes;
    if(bytes.size() < 12 || bytes.size() > frame || bytes.size() > limit-used)
      return foundation::make_unexpected(error(IpcErrc::Budget));
    (kind == Queue::Control ? s->control : s->notifications).emplace_back(bytes.begin(),bytes.end());
    used += bytes.size();
    if(s->started && !s->write_wakeup) { s->write_wakeup = true; wake = true; }
  }
  if(wake) SetEvent(s->wake.native_handle());
  return {};
}
struct FrameReservation::State {
  std::shared_ptr<Connection::State> owner;
  std::vector<std::byte> bytes;
  Queue kind;
  bool used = false;
};
FrameReservation::FrameReservation(std::unique_ptr<State> state) : state_(std::move(state)) {}
FrameReservation::~FrameReservation() {
  if(!state_->used) {
    std::lock_guard lock(state_->owner->mutex);
    auto &used = state_->kind == Queue::Control ? state_->owner->control_bytes : state_->owner->notification_bytes;
    used -= state_->bytes.size();
  }
}
std::size_t FrameReservation::size() const noexcept { return state_->bytes.size(); }
foundation::Result<std::unique_ptr<FrameReservation>> Connection::reserve(std::span<const std::byte> bytes, Queue kind) {
  auto s = state_;
  auto ticket = std::make_unique<FrameReservation::State>();
  ticket->owner = s; ticket->kind = kind;
  const auto maximum = kind == Queue::Control ? s->options.frame_bytes : s->options.notification_bytes;
  if(bytes.size() < 12 || bytes.size() > maximum) return foundation::make_unexpected(error(IpcErrc::Budget));
  ticket->bytes.assign(bytes.begin(),bytes.end());
  auto result = std::unique_ptr<FrameReservation>(new FrameReservation(std::move(ticket)));
  // 失败票据未入账，不得析构扣减其他帧配额。
  result->state_->used = true;
  std::lock_guard lock(s->mutex);
  if(s->closed || s->quarantined || s->draining) return foundation::make_unexpected(error(IpcErrc::Closed));
  auto &used = kind == Queue::Control ? s->control_bytes : s->notification_bytes;
  const auto limit = kind == Queue::Control ? s->options.control_queue_bytes : s->options.notification_queue_bytes;
  if(bytes.size() > limit-used) return foundation::make_unexpected(error(IpcErrc::Budget));
  used += bytes.size(); result->state_->used = false;
  return result;
}
Start Connection::start_now(FrameReservation &reservation, std::span<const std::byte> exact_bytes) noexcept {
  auto &ticket = *reservation.state_;
  if(ticket.owner.get() != state_.get() || ticket.used) return Start::NotStarted;
  auto &s = *state_;
  std::unique_lock lock(s.mutex,std::try_to_lock);
  if(!lock || s.closed || s.quarantined || s.draining || !s.started || s.write_active || !s.control.empty()) return Start::NotStarted;
  if(!exact_bytes.empty()) {
    if(exact_bytes.size() != ticket.bytes.size()) return Start::NotStarted;
    std::copy(exact_bytes.begin(),exact_bytes.end(),ticket.bytes.begin());
  }
  s.prefix_byte = ticket.bytes.front();
  ResetEvent(s.prefix_event.get());
  DWORD written = 0;
  const BOOL complete = WriteFile(s.pipe.native_handle(),&s.prefix_byte,1,&written,&s.prefix);
  const DWORD code = complete ? ERROR_SUCCESS : GetLastError();
  if(complete && written == 0) return Start::NotStarted;
  if(complete && written == 1) {
    s.writing.swap(ticket.bytes); s.writing_kind = ticket.kind;
    ticket.used = true; s.write_active = true; s.suffix_pending = true;
    SetEvent(s.wake.native_handle());
    return Start::Started;
  }
  s.prefix_pending = code == ERROR_IO_PENDING;
  s.quarantined = true;
  SetEvent(s.wake.native_handle());
  return Start::Unknown;
}
foundation::Result<std::unique_ptr<control::FrameBufferReservation>> Connection::reserve_frame(std::size_t size, Queue queue) {
  const auto maximum = queue == Queue::Control ? state_->options.frame_bytes : state_->options.notification_bytes;
  if(size < 12 || size > maximum) return foundation::make_unexpected(error(IpcErrc::Budget));
  std::vector<std::byte> placeholder(size);
  auto ticket = reserve(placeholder,queue);
  if(!ticket) return foundation::make_unexpected(ticket.error());
  return std::unique_ptr<control::FrameBufferReservation>(std::move(*ticket));
}
Start Connection::start_frame(control::FrameBufferReservation &ticket, std::span<const std::byte> bytes) noexcept {
  auto own = dynamic_cast<FrameReservation*>(&ticket);
  if(!own || bytes.empty()) return Start::NotStarted;
  return start_now(*own,bytes);
}
void Connection::close() noexcept {
  auto s = state_;
  { std::lock_guard lock(s->mutex);
    if(!s->started) {
      s->closed = true;
      s->finished = true; s->drained.notify_all();
      std::error_code ignored; s->pipe.close(ignored);
      s->io.stop();
      return;
    }
    s->quarantined = true;
  }
  SetEvent(s->wake.native_handle());
}
void Connection::close_after_flush() noexcept {
  auto s = state_;
  { std::lock_guard lock(s->mutex); s->draining = true; }
  SetEvent(s->wake.native_handle());
}
bool Connection::wait_closed(std::chrono::milliseconds timeout) {
  std::unique_lock lock(state_->mutex);
  return state_->drained.wait_for(lock,timeout,[&] { return state_->finished; });
}

struct Server::State : std::enable_shared_from_this<Server::State> {
  PipeOptions options;
  Accepted accepted;
  HANDLE stopping = CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE first = INVALID_HANDLE_VALUE;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  std::wstring name;
  std::thread thread;
  std::mutex mutex;
  std::vector<std::weak_ptr<Connection>> connections;
  ~State() { finish_thread(thread); if(first != INVALID_HANDLE_VALUE) CloseHandle(first); if(stopping) CloseHandle(stopping); if(descriptor) LocalFree(descriptor); }
  HANDLE create(bool initial) {
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (initial ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      static_cast<DWORD>(options.connections+1),65536,65536,0,&security);
  }
  void run() {
    HANDLE handle = std::exchange(first,INVALID_HANDLE_VALUE);
    while(WaitForSingleObject(stopping,0) != WAIT_OBJECT_0) {
      if(handle == INVALID_HANDLE_VALUE) break;
      OVERLAPPED overlapped{};
      overlapped.hEvent = CreateEventW(nullptr,TRUE,FALSE,nullptr);
      if(!overlapped.hEvent) { CloseHandle(handle); break; }
      BOOL connected = ConnectNamedPipe(handle,&overlapped);
      DWORD status = connected ? ERROR_SUCCESS : GetLastError();
      HANDLE waits[]{stopping,overlapped.hEvent};
      if(status == ERROR_IO_PENDING) {
        if(WaitForMultipleObjects(2,waits,FALSE,INFINITE) != WAIT_OBJECT_0+1) {
          CancelIoEx(handle,&overlapped); DWORD transferred;
          GetOverlappedResult(handle,&overlapped,&transferred,TRUE);
          CloseHandle(overlapped.hEvent); CloseHandle(handle); return;
        }
        DWORD transferred;
        if(!GetOverlappedResult(handle,&overlapped,&transferred,FALSE)) status = GetLastError();
        else status = ERROR_SUCCESS;
      }
      CloseHandle(overlapped.hEvent);
      if(status != ERROR_SUCCESS && status != ERROR_PIPE_CONNECTED) { CloseHandle(handle); break; }
      NativeHandle accepted_handle{std::exchange(handle,INVALID_HANDLE_VALUE)};
      auto s = std::make_shared<Connection::State>(std::move(accepted_handle),options);
      auto connection = std::shared_ptr<Connection>(new Connection(s));
      bool admitted = false;
      { std::lock_guard lock(mutex);
        std::erase_if(connections,[](auto &v) {
          auto c = v.lock(); return !c || c->wait_closed(std::chrono::milliseconds(0));
        });
        if(connections.size() < options.connections) { connections.push_back(connection); admitted = true; }
      }
      // 首帧读入后才能 impersonate；接受回调应 start，业务回调内 peer 才已验证。
      if(admitted) { try { accepted(connection); } catch(...) { connection->close(); } }
      else connection->close();
      handle = create(false);
    }
    if(handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  }
};
Server::Server(std::shared_ptr<State> state) : state_(std::move(state)) {}
Server::~Server() { close(); }
void Server::close() noexcept {
  auto s = state_;
  SetEvent(s->stopping); finish_thread(s->thread);
  std::lock_guard lock(s->mutex);
  for(auto &entry : s->connections) if(auto connection = entry.lock()) connection->close();
}
foundation::Result<std::unique_ptr<Server>> Server::listen(PipeOptions options, Accepted accepted) try {
  auto name = NativeSecurity::endpoint(options.instance);
  if(!name || !valid(options) || !accepted) return foundation::make_unexpected(error(IpcErrc::InvalidEndpoint));
  auto s = std::make_shared<State>();
  auto security = NativeSecurity::descriptor(options);
  if(!security) return foundation::make_unexpected(security.error());
  s->descriptor = *security;
  s->options = std::move(options); s->accepted = std::move(accepted);
  s->name = std::move(*name);
  if(!s->stopping || (s->first = s->create(true)) == INVALID_HANDLE_VALUE) return foundation::make_unexpected(error(IpcErrc::System));
  auto result = std::unique_ptr<Server>(new Server(s));
  s->thread = std::thread([s] { try { s->run(); } catch(...) { SetEvent(s->stopping); } });
  return result;
} catch(...) {
  return foundation::make_unexpected(error(IpcErrc::System));
}
foundation::Result<std::shared_ptr<Connection>> connect(const PipeOptions &options) try {
  auto name = NativeSecurity::endpoint(options.instance);
  if(!name || !valid(options)) return foundation::make_unexpected(error(IpcErrc::InvalidEndpoint));
  HANDLE pipe = CreateFileW(name->c_str(), FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,nullptr);
  if(pipe == INVALID_HANDLE_VALUE) return foundation::make_unexpected(error(IpcErrc::System));
  NativeHandle owned_pipe{pipe};
  auto identity = NativeSecurity::server(pipe,options.expected_server_sid);
  if(!identity) return foundation::make_unexpected(identity.error());
  auto s = std::make_shared<Connection::State>(std::move(owned_pipe),options); s->identity.emplace(std::move(*identity));
  return std::shared_ptr<Connection>(new Connection(std::move(s)));
} catch(...) {
  return foundation::make_unexpected(error(IpcErrc::System));
}
}
