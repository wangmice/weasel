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

 protected:
  void _ProcessPipeThread(HANDLE pipe, ServerHandler const& handler);

  std::mutex m_workers_mutex;
  std::map<HANDLE, std::shared_ptr<boost::thread>> m_workers;
};

}  // namespace weasel
