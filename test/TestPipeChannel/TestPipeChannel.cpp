// TestPipeChannel.cpp : integration tests for the weasel named pipe IPC layer.
//
// Uses a private pipe name (never the live WeaselNamedPipe) so the tests can
// run while the real WeaselServer is active.

#include <windows.h>
#include <atlbase.h>
#include <wtl/atlapp.h>

#include <WeaselIPC.h>
#include <PipeChannel.h>
#include <PipeServer.h>

#include <boost/thread.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

CAppModule _Module;

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cout << "[FAIL] " << what << std::endl;
  } else {
    std::cout << "[ok] " << what << std::endl;
  }
}

static std::wstring unique_pipe_name(const wchar_t* tag) {
  std::wstring name = weasel::GetPipeName();
  name += L"_UnitTest_";
  name += tag;
  wchar_t pid[16];
  swprintf_s(pid, L"%u", GetCurrentProcessId());
  name += pid;
  return name;
}

using ClientChannel = weasel::PipeChannel<weasel::PipeMessage>;

/* Client-side connect with startup grace: the server listener thread may
 * not have created its pipe instance yet when we first try. */
static bool connect_with_retry(ClientChannel& client, int ms = 2000) {
  for (int waited = 0; waited < ms; waited += 25) {
    if (client.Connect())
      return true;
    Sleep(25);
  }
  return false;
}

/* Smoke test: request/response roundtrip through Listen + Transact */
static void test_roundtrip() {
  std::wstring name = unique_pipe_name(L"smoke");
  weasel::PipeServer* server =
      new weasel::PipeServer(std::wstring(name));
  auto handler = [](weasel::PipeMessage msg,
                    weasel::PipeServer::Respond resp) {
    resp(msg.wParam + 1);
  };
  boost::thread listener([server, &handler] { server->Listen(handler); });

  ClientChannel client{std::wstring(name)};
  check(connect_with_retry(client), "smoke: connect");
  weasel::PipeMessage req{WEASEL_IPC_ECHO, 41, 0};
  DWORD resp = client.Transact(req);
  check(resp == 42, "smoke: roundtrip reply");
}

int main() {
  test_roundtrip();
  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  // The listener threads are still blocked in ConnectNamedPipe on purpose;
  // skip static destructors and exit immediately.
  std::cout.flush();
  ExitProcess(g_failures ? 1 : 0);
}
