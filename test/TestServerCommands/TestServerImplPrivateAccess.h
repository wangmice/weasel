#pragma once

#include <PipeServer.h>
#include "../../WeaselIPCServer/WeaselServerImpl.h"

namespace weasel {

class ServerImplTestAccess {
 public:
  static PipeServer& Channel(ServerImpl& server) { return *server.channel; }

  static std::unique_ptr<boost::thread>& PipeThread(ServerImpl& server) {
    return server.pipeThread;
  }

  static void StopPipeServer(ServerImpl& server) { server._StopPipeServer(); }

  static void SetRuntimeState(ServerImpl& server, ServerRuntimeState* state) {
    server.m_runtimeState = state;
  }

  static void SetDispatchProcessOwner(ServerImpl& server,
                                      std::optional<ClientProcessToken> owner) {
    server.m_dispatchProcessOwner = owner;
  }

  static std::optional<ClientProcessToken> MaintenanceOwner(
      const ServerImpl& server) {
    return server.m_maintenanceOwner;
  }

  static void NotifyClientProcessExit(ServerImpl& server,
                                      ClientProcessToken token) {
    server._OnClientProcessExit(token);
  }

  template <typename Resp>
  static void HandlePipeMessage(ServerImpl& server,
                                PipeMessage message,
                                Resp response) {
    server.HandlePipeMessage(message, response);
  }
};

}  // namespace weasel
