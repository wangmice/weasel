// Exercise the production command dispatcher and TSF reconnect path on a
// private pipe. No live service, registration, or Rime data is touched.
#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselIPCServer/stdafx.h"
#include <WeaselIPCData.h>
#include <WeaselUtility.h>
#include <KeyEvent.h>
#include <ResponseParser.h>
#include <logging.h>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/thread/tss.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <PipeChannel.h>
#include "../../WeaselIPCServer/SecurityAttribute.h"

// Only expose project internals, after loading their system dependencies.
// This lets the test use an isolated pipe and the dispatcher's actual mutex
// without adding test-only APIs to the production code.
#define private public
#include <WeaselIPC.h>
#include <PipeServer.h>
#include "../../WeaselIPC/WeaselClientImpl.h"
#include "../../WeaselIPCServer/WeaselServerImpl.h"
#include "../../WeaselTSF/WeaselTSF.h"
#undef private

#include "../../WeaselTSF/CandidateList.h"
#include "../../WeaselTSF/LanguageBar.h"
#include "../../WeaselTSF/Compartment.h"
// g_api_mutex and HandlePipeMessage's template definition are private to this
// translation unit in production; include the implementation to exercise both.
#include "../../WeaselIPCServer/WeaselServerImpl.cpp"

CAppModule _Module;
static int g_failures = 0;

class TestChannelAccess : public weasel::PipeChannelBase {
 public:
  static void SetName(weasel::PipeChannelBase& channel,
                      const std::wstring& name) {
    channel.*&TestChannelAccess::pname = name;
  }
};

static void check(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

class SessionHandler : public weasel::RequestHandler {
 public:
  DWORD FindSession(DWORD id) override {
    ++echo_calls;
    return sessions.count(id) ? id : 0;
  }
  DWORD AddSession(LPWSTR buffer, EatLine eat) override {
    if (maintaining)
      return 0;
    DWORD id = ++next_session;
    sessions.insert(id);
    ++sessions_created;
    std::wstring response = L"action=status\nstatus.ascii_mode=0\n.\n";
    eat(response);
    return id;
  }
  DWORD RemoveSession(DWORD id) override {
    sessions.erase(id);
    return id;
  }
  void StartMaintenance() override {
    sessions.clear();
    maintaining = true;
  }
  void EndMaintenance() override { maintaining = false; }
  BOOL ProcessKeyEvent(weasel::KeyEvent key, DWORD id, EatLine eat) override {
    if (!sessions.count(id)) {
      ++invalid_keys;
      return FALSE;
    }
    ++accepted_keys;
    return TRUE;
  }
  void SetOption(DWORD id, const std::string& opt, bool val) override {
    last_session = id;
    last_option = opt;
    last_value = val;
    ++option_calls;
  }

  std::set<DWORD> sessions;
  DWORD next_session = 0;
  bool maintaining = false;
  std::atomic<int> echo_calls{0};
  std::atomic<int> sessions_created{0};
  std::atomic<int> accepted_keys{0};
  std::atomic<int> invalid_keys{0};
  std::atomic<int> option_calls{0};
  DWORD last_session = 0;
  std::string last_option;
  bool last_value = false;
};

static void test_pipe_ascii_commands() {
  SessionHandler handler;
  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  for (UINT command : {ID_WEASELTRAY_ENABLE_ASCII,
                       ID_WEASELTRAY_DISABLE_ASCII}) {
    bool replied = false;
    DWORD result = 0;
    int before = handler.option_calls;
    try {
      // This is the same lock boundary as ServerImpl::Run's pipe listener.
      std::lock_guard guard(g_api_mutex);
      server.HandlePipeMessage(
          {WEASEL_IPC_TRAY_COMMAND, command, 42},
          [&](DWORD response) {
            replied = true;
            result = response;
          });
    } catch (const std::system_error& error) {
      std::printf("  dispatch threw: %s\n", error.what());
    }
    check(replied && result == TRUE, "pipe command completes under the API lock");
    check(handler.option_calls == before + 1 && handler.last_session == 42 &&
              handler.last_option == "ascii_mode" &&
              handler.last_value == (command == ID_WEASELTRAY_ENABLE_ASCII),
          "pipe command sets the requested option exactly once");
  }
}

static void test_window_ascii_commands() {
  SessionHandler handler;
  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  for (UINT command : {ID_WEASELTRAY_ENABLE_ASCII,
                       ID_WEASELTRAY_DISABLE_ASCII}) {
    std::promise<void> started;
    auto ready = started.get_future();
    std::promise<void> completed;
    auto done = completed.get_future();
    BOOL handled = TRUE;
    int before = handler.option_calls;
    std::unique_lock lock(g_api_mutex);
    std::thread window_thread([&] {
      started.set_value();
      server.OnCommand(WM_COMMAND, command, 43, handled);
      completed.set_value();
    });
    ready.wait();
    check(done.wait_for(std::chrono::milliseconds(100)) ==
                  std::future_status::timeout &&
              handler.option_calls == before,
          "window command waits for the pipe dispatcher's API lock");
    lock.unlock();
    window_thread.join();
    check(handled && handler.option_calls == before + 1 &&
              handler.last_session == 43 && handler.last_option == "ascii_mode" &&
              handler.last_value == (command == ID_WEASELTRAY_ENABLE_ASCII),
          "window command sets the requested option exactly once");
  }
}

static bool connect_with_retry(weasel::Client& client) {
  for (int waited = 0; waited < 2000; waited += 25) {
    if (client.Connect())
      return true;
    Sleep(25);
  }
  return false;
}

static void test_maintenance_session_recovery() {
  SessionHandler handler;
  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  std::wstring pipe_name = weasel::GetPipeName() + L"_SessionRecoveryTest_" +
                           std::to_wstring(GetCurrentProcessId());
  TestChannelAccess::SetName(*server.channel, pipe_name);
  server.pipeThread = std::make_unique<boost::thread>([&] {
    server.channel->Listen([&](weasel::PipeMessage msg,
                              weasel::PipeServer::Respond respond) {
      DWORD result = 0;
      {
        std::lock_guard guard(g_api_mutex);
        server.HandlePipeMessage(msg, [&](DWORD reply) { result = reply; });
      }
      respond(result);
    });
  });

  WeaselTSF tsf;
  TestChannelAccess::SetName(tsf.m_client.m_pImpl->channel, pipe_name);
  weasel::Client deployer;
  TestChannelAccess::SetName(deployer.m_pImpl->channel, pipe_name);
  bool connected = connect_with_retry(tsf.m_client) &&
                   connect_with_retry(deployer);
  check(connected, "TSF and deployer connect to the isolated server");
  if (!connected)
    return;

  tsf.m_client.StartSession();
  DWORD original_session = tsf.m_client.m_pImpl->_SessionId();
  check(original_session != 0, "TSF establishes its initial session");
  int echo_before = handler.echo_calls;
  check(tsf._EnsureServerConnected(), "valid session remains usable");
  check(handler.echo_calls > echo_before && handler.sessions_created == 1,
        "valid session is checked without creating another session");

  // A different client performs deployment while the TSF pipe stays open.
  deployer.StartMaintenance();
  check(tsf.m_client.IsSessionActive(),
        "TSF still has an open pipe and cached session after maintenance starts");
  check(!tsf._EnsureServerConnected(),
        "TSF refuses to use an invalid session during maintenance");
  deployer.EndMaintenance();
  check(tsf._EnsureServerConnected(), "TSF reconnects after maintenance ends");
  check(tsf.m_client.m_pImpl->_SessionId() != original_session &&
            handler.sessions_created == 2,
        "TSF replaces the invalid session without a focus switch or restart");
  check(tsf.m_client.ProcessKeyEvent(weasel::KeyEvent('a')),
        "the first post-maintenance key is accepted");
  check(handler.accepted_keys == 1 && handler.invalid_keys == 0,
        "the key is delivered once and never sent to an invalid session");
}

int main() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr))
    return 1;
  g_hInst = GetModuleHandle(nullptr);
  test_pipe_ascii_commands();
  test_window_ascii_commands();
  test_maintenance_session_recovery();
  CoUninitialize();
  std::printf("Failures: %d\n", g_failures);
  return g_failures ? 1 : 0;
}
