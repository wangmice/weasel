#include <windows.h>

#include <PipeChannel.h>
#include <PipeServer.h>
#include <WeaselIPC.h>

#include <boost/thread.hpp>

#include <cstdlib>
#include <future>
#include <iostream>
#include <string>

namespace {

using ClientChannel = weasel::PipeChannel<weasel::PipeMessage>;

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] ConnectionContext: " << what << std::endl;
  std::abort();
}

bool connect_with_retry(ClientChannel& client) {
  for (int waited = 0; waited < 2000; waited += 25) {
    if (client.Connect())
      return true;
    Sleep(25);
  }
  return false;
}

struct ConnectionContextSelfTest {
  ConnectionContextSelfTest() {
    std::wstring name = weasel::GetPipeName();
    name += L"_ConnectionContext_";
    name += std::to_wstring(GetCurrentProcessId());

    weasel::PipeServer server{std::wstring(name)};
    std::promise<weasel::PipeServer::ConnectionContext> observed;
    auto observed_future = observed.get_future();
    auto handler = [&](const weasel::PipeServer::ConnectionContext& context,
                       weasel::PipeMessage message,
                       weasel::PipeServer::Respond respond) {
      observed.set_value(context);
      respond(message.wParam + 1);
    };
    boost::thread listener([&] { server.Listen(handler); });

    ClientChannel client{std::wstring(name)};
    require(connect_with_retry(client), "private client connects");
    weasel::PipeMessage request{WEASEL_IPC_ECHO, 41, 0};
    require(client.Transact(request) == 42, "context-aware handler replies");

    const auto context = observed_future.get();
    require(context.worker_id != 0, "worker id is stable and nonzero");
    require(context.client_pid.has_value() &&
                *context.client_pid == GetCurrentProcessId(),
            "server obtains the authoritative client PID from the pipe");

    client.Disconnect();
    server.StopListener(listener);
    server.DrainWorkers();
  }
};

ConnectionContextSelfTest g_connection_context_self_test;

}  // namespace
