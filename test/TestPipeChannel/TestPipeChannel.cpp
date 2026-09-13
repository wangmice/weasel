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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
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

/* 1.2: a client that stops reading must not block the server from serving
 * other clients (regression for FlushFileBuffers under the api mutex) */
static void test_hung_client_does_not_freeze_others() {
  std::wstring name = unique_pipe_name(L"flush");
  weasel::PipeServer* server =
      new weasel::PipeServer(std::wstring(name));
  std::mutex api_mutex;  // mirrors ServerImpl's request serialization
  auto handler = [&api_mutex](weasel::PipeMessage msg,
                              weasel::PipeServer::Respond resp) {
    std::lock_guard<std::mutex> lock(api_mutex);
    resp(msg.wParam + 1);
  };
  boost::thread listener([server, &handler] { server->Listen(handler); });

  // client A: raw pipe client that sends a request and never reads the reply
  HANDLE a = INVALID_HANDLE_VALUE;
  for (int waited = 0; waited < 2000; waited += 25) {
    a = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (a != INVALID_HANDLE_VALUE)
      break;
    Sleep(25);
  }
  check(a != INVALID_HANDLE_VALUE, "1.2: hung client connected");
  weasel::PipeMessage req{WEASEL_IPC_ECHO, 1, 0};
  DWORD written = 0;
  ::WriteFile(a, &req, sizeof(req), &written, NULL);

  // client B: ordinary request/response, must complete promptly
  ClientChannel b{std::wstring(name)};
  check(connect_with_retry(b), "1.2: B connect");
  bool b_ok = false;
  boost::thread b_thread([&b, &b_ok] {
    weasel::PipeMessage r{WEASEL_IPC_ECHO, 41, 0};
    try {
      b_ok = (b.Transact(r) == 42);
    } catch (...) {
    }
  });
  bool served_in_time = b_thread.timed_join(boost::posix_time::seconds(3));
  check(served_in_time, "1.2: B served while A hangs");
  check(b_ok, "1.2: B got correct reply");
  if (!served_in_time)
    b_thread.detach();  // worker is stuck on the hung client; drop it
  ::CloseHandle(a);
}

/* 1.3: the server must see the request body from its first line on
 * (START_SESSION bodies historically lost their first 6 characters) */
static void test_start_session_body_offset() {
  std::wstring name = unique_pipe_name(L"body");
  ExposedServer* server = new ExposedServer(std::wstring(name));
  std::promise<std::wstring> body_promise;
  auto body_future = body_promise.get_future();
  bool captured = false;
  auto handler = [server, &body_promise, &captured](
                     weasel::PipeMessage msg,
                     weasel::PipeServer::Respond resp) {
    if (msg.Msg == WEASEL_IPC_START_SESSION && !captured) {
      captured = true;
      body_promise.set_value(std::wstring((LPWSTR)server->ReceiveBuffer()));
    }
    resp(7);
  };
  boost::thread listener([server, &handler] { server->Listen(handler); });

  ClientChannel client{std::wstring(name)};
  check(connect_with_retry(client), "1.3: connect");
  client << L"action=session\n";
  client << L"session.client_app=notepad.exe\n";
  client << L".\n";
  weasel::PipeMessage req{WEASEL_IPC_START_SESSION, 0, 0};
  DWORD resp = client.Transact(req);
  check(resp == 7, "1.3: start session reply");

  check(body_future.wait_for(std::chrono::seconds(3)) ==
            std::future_status::ready,
        "1.3: body captured");
  if (body_future.valid()) {
    std::wstring body = body_future.get();
    check(body.rfind(L"action=session", 0) == 0,
          "1.3: body starts with 'action=session' line");
    check(body.find(L"session.client_app=notepad.exe") != std::wstring::npos,
          "1.3: client_app line intact");
  }
}

int main() {
  test_roundtrip();
  test_pipe_connected_race();
  test_hung_client_does_not_freeze_others();
  test_start_session_body_offset();
  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  // The listener threads are still blocked in ConnectNamedPipe on purpose;
  // skip static destructors and exit immediately.
  std::cout.flush();
  ExitProcess(g_failures ? 1 : 0);
}
