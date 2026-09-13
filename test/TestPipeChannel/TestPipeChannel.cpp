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

/* Expose protected seams for deterministic tests */
class ExposedServer : public weasel::PipeServer {
 public:
  using PipeServer::PipeServer;
  HANDLE ExposedCreate() { return _CreateServerPipe(pname); }
  HANDLE ExposedAccept(HANDLE pipe) { return _AcceptServerPipe(pipe); }
  void ExposedReceive(HANDLE pipe, LPVOID msg, size_t len) {
    _Receive(pipe, msg, len);
  }
};

/* 1.1: a client that connects between CreateNamedPipe and ConnectNamedPipe
 * (ConnectNamedPipe failing with ERROR_PIPE_CONNECTED) must not be dropped */
static void test_pipe_connected_race() {
  std::wstring name = unique_pipe_name(L"e535");
  ExposedServer server{std::wstring(name)};

  HANDLE instance = server.ExposedCreate();
  check(instance != INVALID_HANDLE_VALUE, "1.1: create instance");

  HANDLE client = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING, 0, NULL);
  check(client != INVALID_HANDLE_VALUE, "1.1: early client connect");

  bool accepted = false;
  try {
    accepted = (server.ExposedAccept(instance) == instance);
  } catch (DWORD err) {
    std::cout << "  accept threw error " << err << std::endl;
  }
  check(accepted, "1.1: ERROR_PIPE_CONNECTED accepted as connected");

  if (accepted) {
    weasel::PipeMessage req{WEASEL_IPC_ECHO, 7, 0};
    DWORD written = 0;
    check(::WriteFile(client, &req, sizeof(req), &written, NULL) &&
              written == sizeof(req),
          "1.1: client sends on racing connection");
    weasel::PipeMessage got{};
    try {
      server.ExposedReceive(instance, &got, sizeof(got));
      check(got.Msg == WEASEL_IPC_ECHO && got.wParam == 7,
            "1.1: server reads message on racing connection");
    } catch (DWORD err) {
      check(false, "1.1: server reads message on racing connection");
      std::cout << "  receive threw error " << err << std::endl;
    }
  }

  ::CloseHandle(client);
  ::DisconnectNamedPipe(instance);
  ::CloseHandle(instance);
}

int main() {
  test_roundtrip();
  test_pipe_connected_race();
  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  // The listener threads are still blocked in ConnectNamedPipe on purpose;
  // skip static destructors and exit immediately.
  std::cout.flush();
  ExitProcess(g_failures ? 1 : 0);
}
