#pragma once

#include <WeaselSessionLifecycle.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace weasel {

class ClientProcessTracker {
 public:
  using ExitCallback = std::function<void(ClientProcessToken)>;

  class ExitWatch {
   public:
    virtual ~ExitWatch() = default;
  };

  class Process {
   public:
    virtual ~Process() = default;
    virtual bool IsRunning() const = 0;
    virtual std::unique_ptr<ExitWatch> WatchExit(
        std::function<void()> on_exit) = 0;
  };

  class Backend {
   public:
    virtual ~Backend() = default;
    virtual std::unique_ptr<Process> Open(std::uint32_t pid) = 0;
  };

  explicit ClientProcessTracker(ExitCallback on_exit)
      : ClientProcessTracker(std::move(on_exit), CreateDefaultBackend()) {}

  ClientProcessTracker(ExitCallback on_exit, std::shared_ptr<Backend> backend)
      : on_exit_(std::move(on_exit)), backend_(std::move(backend)) {
    retire_thread_ = std::thread([this] { RetireLoop(); });
  }

  ~ClientProcessTracker() { Stop(); }

  ClientProcessTracker(const ClientProcessTracker&) = delete;
  ClientProcessTracker& operator=(const ClientProcessTracker&) = delete;

  std::optional<ClientProcessToken> Resolve(std::uint32_t pid) {
    if (!pid || !backend_)
      return std::nullopt;

    // Serialize PID replacement/open registration without making exit callbacks
    // take this gate. Stop only holds it while publishing stopping state, never
    // while draining callbacks.
    std::unique_lock<std::mutex> resolve_lock(resolve_mutex_);

    std::unique_ptr<Entry> stale;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_ || stopped_)
        return std::nullopt;
      const auto existing = entries_.find(pid);
      if (existing != entries_.end()) {
        if (!existing->second->exit_notified &&
            existing->second->process->IsRunning()) {
          return existing->second->token;
        }
        stale = std::move(existing->second);
        entries_.erase(existing);
      }
    }
    // Watch destruction synchronously drains an old callback. Do not hold the
    // metadata mutex while doing so; callbacks are allowed to query tracker
    // state and the same numeric PID may subsequently be reused.
    stale.reset();

    std::unique_ptr<Process> process = backend_->Open(pid);
    if (!process || !process->IsRunning())
      return std::nullopt;

    auto entry = std::make_unique<Entry>();
    entry->pid = pid;
    entry->process = std::move(process);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_ || stopped_)
        return std::nullopt;
      entry->token = NextTokenLocked();
      entries_[pid] = std::move(entry);
    }

    Entry* installed = nullptr;
    ClientProcessToken token;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = entries_.find(pid);
      if (it == entries_.end())
        return std::nullopt;
      installed = it->second.get();
      token = installed->token;
    }

    std::unique_ptr<ExitWatch> watch = installed->process->WatchExit(
        [this, pid, token]() { HandleExit(pid, token); });
    if (!watch) {
      std::unique_ptr<Entry> failed;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(pid);
        if (it != entries_.end() && it->second->token == token) {
          failed = std::move(it->second);
          entries_.erase(it);
        }
      }
      failed.reset();
      return std::nullopt;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = entries_.find(pid);
      if (it == entries_.end() || it->second->token != token)
        return std::nullopt;
      it->second->watch = std::move(watch);
    }
    return token;
  }

  // Call after acquiring the session/API serialization lock and immediately
  // before binding a new session to this token. If the process exited after
  // Resolve but before this check, creation can be rejected; if it exits just
  // after this check, the exit callback queues behind that serialization lock
  // and observes the newly-bound session.
  bool IsAlive(ClientProcessToken token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!token.value || stopping_ || stopped_)
      return false;
    for (const auto& [pid, entry] : entries_) {
      (void)pid;
      if (entry->token == token)
        return !entry->exit_notified && entry->process->IsRunning();
    }
    return false;
  }

  void Stop() {
    std::vector<std::unique_ptr<Entry>> entries;
    {
      std::unique_lock<std::mutex> resolve_lock(resolve_mutex_);
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_)
        return;
      if (stopping_) {
        // Another Stop is already draining. This object is normally stopped
        // from one lifecycle thread; avoid waiting while holding resolve gate.
        return;
      }
      stopping_ = true;
      entries.reserve(entries_.size());
      for (auto& [pid, entry] : entries_) {
        (void)pid;
        entries.push_back(std::move(entry));
      }
      entries_.clear();
    }

    // Entry member order destroys the wait before its process handle. Windows
    // ExitWatch uses UnregisterWaitEx(..., INVALID_HANDLE_VALUE), so all active
    // callbacks have returned when this loop completes.
    entries.clear();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      retire_stop_ = true;
    }
    retire_cv_.notify_all();
    if (retire_thread_.joinable() &&
        retire_thread_.get_id() != std::this_thread::get_id()) {
      retire_thread_.join();
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
  }

 private:
  struct Entry {
    std::uint32_t pid = 0;
    ClientProcessToken token;
    std::unique_ptr<Process> process;
    std::unique_ptr<ExitWatch> watch;
    bool exit_notified = false;
    // Intrusive retirement list avoids allocating from a noexcept wait callback.
    std::unique_ptr<Entry> retired_next;
  };

  ClientProcessToken NextTokenLocked() {
    ++next_token_;
    if (next_token_ == 0)
      ++next_token_;
    return ClientProcessToken{next_token_};
  }

  void HandleExit(std::uint32_t pid, ClientProcessToken token) noexcept {
    ExitCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_ || stopped_)
        return;
      const auto it = entries_.find(pid);
      if (it == entries_.end() || it->second->token != token ||
          it->second->exit_notified) {
        return;
      }

      it->second->exit_notified = true;
      auto retired = std::move(it->second);
      entries_.erase(it);
      retired->retired_next = std::move(retired_head_);
      retired_head_ = std::move(retired);
      callback = on_exit_;
    }

    // Never destroy Win32ExitWatch from its own registered-wait callback:
    // UnregisterWaitEx(..., INVALID_HANDLE_VALUE) blocks for callbacks to exit.
    // A dedicated reaper owns destruction after this callback releases locks.
    retire_cv_.notify_one();

    if (callback) {
      try {
        callback(token);
      } catch (...) {
        // Windows wait callbacks must never let exceptions escape into the
        // thread-pool/registered-wait machinery. Session cleanup is best-effort
        // at this boundary; server teardown/maintenance remains a final sweep.
      }
    }
  }

  void RetireLoop() noexcept {
    for (;;) {
      std::unique_ptr<Entry> retired;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        retire_cv_.wait(lock,
                        [this] { return retire_stop_ || retired_head_ != nullptr; });
        if (!retired_head_ && retire_stop_)
          return;

        retired = std::move(retired_head_);
        retired_head_ = std::move(retired->retired_next);
        retired->retired_next.reset();
      }

      // Destruction runs outside tracker locks. On Windows this may block in
      // UnregisterWaitEx until the callback that enqueued |retired| returns.
      retired.reset();
    }
  }

#ifdef _WIN32
  class Win32ExitWatch final : public ExitWatch {
   public:
    struct CallbackState {
      std::function<void()> callback;
    };

    static std::unique_ptr<Win32ExitWatch> Create(
        HANDLE process,
        std::function<void()> callback) {
      auto watch = std::unique_ptr<Win32ExitWatch>(new Win32ExitWatch());
      watch->state_ = std::make_unique<CallbackState>();
      watch->state_->callback = std::move(callback);
      if (!::RegisterWaitForSingleObject(
              &watch->wait_, process, &Win32ExitWatch::OnSignaled,
              watch->state_.get(), INFINITE, WT_EXECUTEONLYONCE)) {
        return nullptr;
      }
      return watch;
    }

    ~Win32ExitWatch() override {
      if (wait_) {
        // Synchronously drain an in-flight callback before CallbackState dies.
        ::UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
        wait_ = nullptr;
      }
    }

   private:
    static VOID CALLBACK OnSignaled(PVOID context, BOOLEAN) noexcept {
      auto* state = static_cast<CallbackState*>(context);
      if (!state || !state->callback)
        return;
      try {
        state->callback();
      } catch (...) {
      }
    }

    HANDLE wait_ = nullptr;
    std::unique_ptr<CallbackState> state_;
  };

  class Win32Process final : public Process {
   public:
    explicit Win32Process(HANDLE process) : process_(process) {}
    ~Win32Process() override {
      if (process_)
        ::CloseHandle(process_);
    }

    bool IsRunning() const override {
      return process_ && ::WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }

    std::unique_ptr<ExitWatch> WatchExit(
        std::function<void()> on_exit) override {
      if (!process_)
        return nullptr;
      return Win32ExitWatch::Create(process_, std::move(on_exit));
    }

   private:
    HANDLE process_ = nullptr;
  };

  class Win32Backend final : public Backend {
   public:
    std::unique_ptr<Process> Open(std::uint32_t pid) override {
      HANDLE process =
          ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
      if (!process)
        return nullptr;
      return std::make_unique<Win32Process>(process);
    }
  };
#endif

  static std::shared_ptr<Backend> CreateDefaultBackend() {
#ifdef _WIN32
    return std::make_shared<Win32Backend>();
#else
    return nullptr;
#endif
  }

  ExitCallback on_exit_;
  std::shared_ptr<Backend> backend_;

  mutable std::mutex mutex_;
  std::mutex resolve_mutex_;
  std::condition_variable retire_cv_;
  std::map<std::uint32_t, std::unique_ptr<Entry>> entries_;
  std::unique_ptr<Entry> retired_head_;
  std::uint64_t next_token_ = 0;
  bool stopping_ = false;
  bool stopped_ = false;
  bool retire_stop_ = false;
  std::thread retire_thread_;
};

}  // namespace weasel
