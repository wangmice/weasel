#pragma once
#include <WeaselIPC.h>
#include <PipeChannel.h>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <windows.h>

namespace weasel {

/* Server side of the weasel named pipe IPC */
class PipeServer : public PipeChannel<DWORD, PipeMessage> {
 public:
  using ServerRunner = std::function<void()>;
  using Respond = std::function<void(Msg)>;
  using ServerHandler = std::function<void(PipeMessage, Respond)>;

  PipeServer(std::wstring&& pn_cmd, SECURITY_ATTRIBUTES* s = NULL);
  ~PipeServer();

 public:
  void Listen(ServerHandler const& handler);
  /* Get a server runner */
  ServerRunner GetServerRunner(ServerHandler const& handler);
  /* Unblock a listener blocked in ConnectNamedPipe (self-connection) */
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
  };

  using WorkerId = std::uint64_t;

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
};

}  // namespace weasel
