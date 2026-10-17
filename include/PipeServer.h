#pragma once
#include <WeaselIPC.h>
#include <PipeChannel.h>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <windows.h>

namespace weasel {

/* Server side of the weasel named pipe IPC */
class PipeServer : public PipeChannel<DWORD, PipeMessage> {
 public:
  using ServerRunner = std::function<void()>;
  using Respond = std::function<void(Msg)>;

  struct ConnectionContext {
    std::uint64_t worker_id = 0;
    std::optional<DWORD> client_pid;
  };

  // Preserve the long-standing two-argument handler for existing internal
  // users/tests. New lifecycle-aware dispatch can opt into the explicit
  // connection context without a wire-format change.
  using ServerHandler = std::function<void(PipeMessage, Respond)>;
  using ContextServerHandler =
      std::function<void(const ConnectionContext&, PipeMessage, Respond)>;

  PipeServer(std::wstring&& pn_cmd,
             SECURITY_ATTRIBUTES* s = NULL,
             std::wstring compatibility_alias = {});
  ~PipeServer();

 public:
  void Listen(ServerHandler const& handler);
  void Listen(ContextServerHandler const& handler) {
    Listen(ServerHandler(
        [this, handler](PipeMessage message, Respond respond) mutable {
          const std::optional<ConnectionContext> context =
              CurrentConnectionContext();
          handler(context.value_or(ConnectionContext{}), message,
                  std::move(respond));
        }));
  }

  /* Get a server runner */
  ServerRunner GetServerRunner(ServerHandler const& handler);
  ServerRunner GetServerRunner(ContextServerHandler const& handler) {
    return [this, handler]() { Listen(handler); };
  }

  // Resolve immutable transport metadata for the worker currently executing
  // this handler. The first request discovers it from the worker registry and
  // the kernel pipe handle; every later request on that worker uses thread-local
  // immutable state, keeping the healthy key path free of the worker mutex and
  // worker-map scans.
  std::optional<ConnectionContext> CurrentConnectionContext() {
    if (auto* cached = current_connection_context_.get())
      return *cached;

    const boost::thread::id current = boost::this_thread::get_id();
    std::optional<ConnectionContext> resolved;
    {
      std::lock_guard<std::mutex> lock(m_workers_mutex);
      for (auto& [worker_id, entry] : m_workers) {
        if (!entry.thread || entry.thread->get_id() != current)
          continue;

        if (!entry.context) {
          ConnectionContext context;
          context.worker_id = worker_id;
          DWORD pid = 0;
          if (::GetNamedPipeClientProcessId(entry.pipe, &pid) && pid != 0)
            context.client_pid = pid;
          entry.context = context;
        }
        resolved = entry.context;
        break;
      }
    }

    if (resolved)
      current_connection_context_.reset(new ConnectionContext(*resolved));
    return resolved;
  }

  /* Unblock listeners blocked in ConnectNamedPipe (self-connection) */
  void WakeListener();
  /* Interrupt and join a thread running Listen(). Repeats both self-wake and
   * synchronous-I/O cancellation so shutdown cannot miss the window between
   * an interruption point and the next blocking ConnectNamedPipe(). */
  void StopListener(boost::thread& listener);
  /* Cancel and join every live connection worker; call only after the
   * listener thread has been joined so no new workers can appear */
  void DrainWorkers();

 protected:
  struct WorkerEntry {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::shared_ptr<boost::thread> thread;
    bool finished = false;
    std::optional<ConnectionContext> context;
  };

  using WorkerId = std::uint64_t;

  void _ListenOn(std::wstring& pipe_name,
                 ServerHandler const& handler,
                 bool compatibility_alias);
  void _WakeListener(const std::wstring& pipe_name);
  void _StopListenerThread(boost::thread& listener,
                           const std::wstring& pipe_name);
  void _ProcessPipeThread(WorkerId worker_id,
                          HANDLE pipe,
                          ServerHandler const& handler);
  /* Create the worker thread and register it under one lock so a worker
   * that finishes instantly can never overtake its own registration. */
  void _LaunchWorker(HANDLE pipe, ServerHandler const& handler);
  void _MarkWorkerFinished(WorkerId worker_id);
  void _WorkerReaperLoop();

  std::mutex m_workers_mutex;
  std::condition_variable m_workers_cv;
  std::map<WorkerId, WorkerEntry> m_workers;
  WorkerId m_next_worker_id = 0;
  bool m_reaper_stop = false;
  std::shared_ptr<boost::thread> m_reaper_thread;
  std::shared_ptr<boost::thread> m_alias_listener;
  boost::thread_specific_ptr<ConnectionContext> current_connection_context_;
};

}  // namespace weasel
