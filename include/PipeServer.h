#pragma once
#include <WeaselIPC.h>
#include <PipeChannel.h>
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

 public:
  void Listen(ServerHandler const& handler);
  /* Get a server runner */
  ServerRunner GetServerRunner(ServerHandler const& handler);
  /* Unblock a listener blocked in ConnectNamedPipe (self-connection) */
  void WakeListener();
  /* Cancel and join every live connection worker; call only after the
   * listener thread has been joined so no new workers can appear */
  void DrainWorkers();

 protected:
  void _ProcessPipeThread(HANDLE pipe, ServerHandler const& handler);
  /* Create the worker thread and register it under one lock so a worker
   * that finishes instantly can never overtake its own registration. */
  void _LaunchWorker(HANDLE pipe, ServerHandler const& handler);
  void _RemoveWorker(HANDLE pipe);

  std::mutex m_workers_mutex;
  std::map<HANDLE, std::shared_ptr<boost::thread>> m_workers;
};

}  // namespace weasel
