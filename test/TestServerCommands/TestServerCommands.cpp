// Exercise the production command dispatcher and TSF reconnect path on a
// private pipe. No live service, registration, or Rime data is touched.
#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
#error TestServerCommands requires the no-launch TSF build for isolation
#endif
#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselIPCServer/stdafx.h"
#include <WeaselIPCData.h>
#include <WeaselSessionIdAllocator.h>
#include <WeaselUtility.h>
#include <KeyEvent.h>
#include <ResponseParser.h>
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
#include <PipeServer.h>
#include "TestClientPrivateAccess.h"
#include "TestServerImplPrivateAccess.h"
#include "TestWeaselTSFPrivateAccess.h"
#include "../../WeaselIPCServer/SecurityAttribute.h"
#include "../../WeaselUI/d2d.h"
#include "../../WeaselTSF/CandidateList.h"
#include "../../WeaselTSF/LanguageBar.h"
#include "../../WeaselTSF/Compartment.h"
#include "../../WeaselTSF/ServerRecoveryCoordinator.h"
// g_api_mutex and HandlePipeMessage's template definition are private to this
// translation unit in production; include the implementation to exercise both.
#include "../../WeaselIPCServer/WeaselServerImpl.cpp"

CAppModule _Module;
static int g_failures = 0;

class TestChannelAccess : public weasel::PipeChannelBase {
 public:
  static void SetName(weasel::PipeChannelBase& channel,
                      const std::wstring& name) {
    channel.*& TestChannelAccess::pname = name;
    channel.*& TestChannelAccess::fallback_pname = std::wstring();
  }
};

static void check(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

static void test_server_recovery_coordinator_state_machine() {
  using Coordinator = weasel::tsf::ServerRecoveryCoordinator;

  {
    Coordinator coordinator;
    std::atomic<bool> go{false};
    std::atomic<int> winners{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 20; ++i) {
      workers.emplace_back([&] {
        while (!go.load(std::memory_order_acquire))
          std::this_thread::yield();
        if (coordinator.TryBegin(1000))
          ++winners;
      });
    }
    go.store(true, std::memory_order_release);
    for (auto& worker : workers)
      worker.join();
    check(winners == 1 && coordinator.WorkerInflight(),
          "recovery coordinator grants ownership to exactly one worker");
    coordinator.FinishBackoff(100, 1000);
  }

  {
    Coordinator coordinator;
    check(coordinator.TryBegin(1000),
          "recovery coordinator starts an initial worker");
    coordinator.FinishBackoff(100, 1000);
    check(coordinator.RetryAfter() == 1100 &&
              !coordinator.TryBegin(1099) && coordinator.TryBegin(1100),
          "Backoff rejects early workers and admits one at the deadline");
    coordinator.FinishHealthy();
  }

  {
    Coordinator coordinator;
    check(coordinator.TryBegin(1000),
          "backoff publication test acquires the owner");
    coordinator.FinishBackoff(100, 1000);
    check(!coordinator.TryBegin(1001) && coordinator.RetryAfter() == 1100,
          "new recovery cannot bypass a published Backoff");
  }

  {
    Coordinator coordinator;
    check(!coordinator.ObserveUnavailableLongEnough(1000, 1500) &&
              !coordinator.ObserveUnavailableLongEnough(2499, 1500) &&
              coordinator.ObserveUnavailableLongEnough(2500, 1500),
          "restart threshold measures one continuous unavailable interval");
    coordinator.ResetUnavailableObservation();
    check(coordinator.UnavailableDuration(5000) == 0 &&
              !coordinator.ObserveUnavailableLongEnough(5000, 1500) &&
              !coordinator.ObserveUnavailableLongEnough(6499, 1500) &&
              coordinator.ObserveUnavailableLongEnough(6500, 1500),
          "lifecycle reset starts a fresh unavailable interval");
  }

  {
    Coordinator coordinator;
    check(coordinator.TryBegin(1000),
          "failure backoff test acquires the first worker");
    coordinator.FinishFailure(1000, 250, 2000);
    check(coordinator.FailureStreak() == 1 &&
              coordinator.RetryAfter() == 1250,
          "first recovery failure uses the base backoff");
    check(coordinator.TryBegin(1250),
          "failure backoff expires at its deadline");
    coordinator.FinishFailure(1250, 250, 2000);
    check(coordinator.FailureStreak() == 2 &&
              coordinator.RetryAfter() == 1750,
          "consecutive recovery failures back off exponentially");
    coordinator.NoteHealthy();
    check(coordinator.FailureStreak() == 0 &&
              coordinator.RetryAfter() == 0 &&
              coordinator.UnavailableSince() == 0,
          "healthy idle recovery resets failure and unavailable state");
  }

  {
    Coordinator coordinator;
    coordinator.ObserveUnavailableLongEnough(1000, 1500);
    check(coordinator.TryBegin(2500),
          "in-flight health ownership test starts a worker");
    coordinator.NoteHealthy();
    check(coordinator.WorkerInflight() &&
              coordinator.UnavailableSince() == 1000,
          "foreground health hints do not revoke an in-flight recovery owner");
    coordinator.FinishHealthy();
    check(!coordinator.WorkerInflight() &&
              coordinator.UnavailableSince() == 0,
          "the recovery owner clears state after a healthy fresh probe");
  }
}

static void test_session_id_allocator_survives_maintenance_clear() {
  DWORD next_id = 0x1000;
  std::set<DWORD> active;
  auto allocate = [&]() {
    return weasel::AllocateMonotonicSessionId(
        next_id, [&](DWORD candidate) { return active.count(candidate) != 0; });
  };

  const DWORD client_a = allocate();
  active.insert(client_a);
  const DWORD client_b = allocate();
  active.insert(client_b);

  // Maintenance destroys all live sessions but stale clients can keep their
  // old IDs on still-connected named pipes. The allocator high-water mark
  // must survive that clear.
  active.clear();
  const DWORD replacement = allocate();

  check(
      client_a == 0x1000 && client_b == 0x1001 && replacement == 0x1002,
      "session ids remain monotonic after maintenance clears active sessions");
  check(replacement != client_a && replacement != client_b,
        "new sessions cannot alias stale client ids after maintenance");
}

static void test_pending_ui_generation_rejects_stale_extent() {
  WeaselTSF tsf;

  auto first = std::make_shared<weasel::Context>();
  first->preedit.str = L"first";
  weasel::Status first_status;
  first_status.composing = true;
  const UINT64 first_generation = tsf._QueuePendingUI(first, first_status);

  auto second = std::make_shared<weasel::Context>();
  second->preedit.str = L"second";
  weasel::Status second_status;
  second_status.composing = true;
  const UINT64 second_generation = tsf._QueuePendingUI(second, second_status);

  check(first_generation != 0 && second_generation != first_generation,
        "pending UI generation advances for a newer response");
  check(!tsf._IsPendingUIGenerationCurrent(first_generation) &&
            tsf._IsPendingUIGenerationCurrent(second_generation),
        "stale extent generation cannot publish newer UI");

  std::shared_ptr<weasel::Context> taken;
  weasel::Status taken_status;
  check(!tsf._TakePendingUI(first_generation, taken, taken_status) &&
            WeaselTSFTestAccess::HasPendingUI(tsf),
        "stale extent leaves the current pending UI untouched");
  check(tsf._TakePendingUI(second_generation, taken, taken_status) && taken &&
            taken->preedit.str == L"second" &&
            !WeaselTSFTestAccess::HasPendingUI(tsf),
        "current extent consumes exactly its own UI snapshot");

  const UINT64 canceled_generation = tsf._QueuePendingUI(second, second_status);
  tsf._InvalidatePendingUI();
  check(!tsf._IsPendingUIGenerationCurrent(canceled_generation) &&
            !WeaselTSFTestAccess::HasPendingUI(tsf),
        "composition teardown invalidates delayed extent callbacks");
}

static void test_panel_recreate_reapplies_window_size() {
  weasel::UI ui;
  weasel::UIStyle style;
  style.font_face = L"Segoe UI";
  style.font_point = 12;
  ui.SetStyle(style);

  weasel::Context context;
  weasel::Text candidate;
  candidate.str = L"candidate-window-size";
  context.cinfo.candies.push_back(candidate);
  weasel::Text label;
  label.str = L"1";
  context.cinfo.labels.push_back(label);
  context.cinfo.comments.emplace_back();
  context.cinfo.highlighted = 0;
  weasel::Status status;
  status.composing = true;
  ui.Update(context, status);

  const bool first_created = ui.Create(nullptr);
  RECT first{};
  const bool first_rect =
      first_created && ui.hwnd() && GetWindowRect(ui.hwnd(), &first);
  const int first_width = first.right - first.left;
  const int first_height = first.bottom - first.top;

  ui.Destroy(false);
  const bool second_created = ui.Create(nullptr);
  RECT second{};
  const bool second_rect =
      second_created && ui.hwnd() && GetWindowRect(ui.hwnd(), &second);
  const int second_width = second.right - second.left;
  const int second_height = second.bottom - second.top;

  check(first_rect && first_width > 10 && first_height > 10,
        "panel first HWND is resized from its 10x10 creation size");
  check(second_rect && second_width == first_width &&
            second_height == first_height && second_width > 10 &&
            second_height > 10,
        "retained panel reapplies layout size to a recreated HWND");

  ui.Destroy(true);
  check(!weasel::DeviceResources::Get().HasLiveGeneration(),
        "panel teardown releases the shared graphics generation lease");
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
    DWORD id = weasel::AllocateMonotonicSessionId(
        next_session,
        [this](DWORD candidate) { return sessions.count(candidate) != 0; });
    if (!id)
      return 0;
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
      std::wstring response = L"action=noop\n";
      response.append(weasel::kInvalidSessionResponseField).append(L"\n.\n");
      if (eat)
        eat(response);
      return FALSE;
    }
    ++accepted_keys;
    if (delay_key_reply)
      Sleep(1500);
    return TRUE;
  }
  void SetOption(DWORD id, const std::string& opt, bool val) override {
    last_session = id;
    last_option = opt;
    last_value = val;
    ++option_calls;
  }

  std::set<DWORD> sessions;
  DWORD next_session = 1;
  bool maintaining = false;
  std::atomic<int> echo_calls{0};
  std::atomic<int> sessions_created{0};
  std::atomic<int> accepted_keys{0};
  std::atomic<bool> delay_key_reply{false};
  std::atomic<int> invalid_keys{0};
  std::atomic<int> option_calls{0};
  DWORD last_session = 0;
  std::string last_option;
  bool last_value = false;
};

static void test_server_runtime_state_tracks_maintenance() {
  SessionHandler handler;
  weasel::ServerRuntimeState runtime{};
  runtime.version = weasel::kServerRuntimeStateVersion;
  runtime.process_id = GetCurrentProcessId();
  runtime.lifecycle = static_cast<LONG>(weasel::ServerLifecycle::Ready);
  runtime.process_creation_time = 1;

  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  weasel::ServerImplTestAccess::SetRuntimeState(server, &runtime);

  server.OnStartMaintenance(WEASEL_IPC_START_MAINTENANCE, 0, 0);
  check(handler.maintaining &&
            runtime.lifecycle ==
                static_cast<LONG>(weasel::ServerLifecycle::Maintenance),
        "maintenance is published before backend teardown");

  server.OnEndMaintenance(WEASEL_IPC_END_MAINTENANCE, 0, 0);
  check(!handler.maintaining &&
            runtime.lifecycle ==
                static_cast<LONG>(weasel::ServerLifecycle::Ready),
        "maintenance completion republishes Ready");

  weasel::ServerImplTestAccess::SetRuntimeState(server, nullptr);
}

static void test_pipe_ascii_commands() {
  SessionHandler handler;
  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  for (UINT command :
       {ID_WEASELTRAY_ENABLE_ASCII, ID_WEASELTRAY_DISABLE_ASCII}) {
    bool replied = false;
    DWORD result = 0;
    int before = handler.option_calls;
    try {
      // This is the same lock boundary as ServerImpl::Run's pipe listener.
      std::lock_guard guard(g_api_mutex);
      weasel::ServerImplTestAccess::HandlePipeMessage(
          server, {WEASEL_IPC_TRAY_COMMAND, command, 42}, [&](DWORD response) {
            replied = true;
            result = response;
          });
    } catch (const std::system_error& error) {
      std::printf("  dispatch threw: %s\n", error.what());
    }
    check(replied && result == TRUE,
          "pipe command completes under the API lock");
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
  for (UINT command :
       {ID_WEASELTRAY_ENABLE_ASCII, ID_WEASELTRAY_DISABLE_ASCII}) {
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
              handler.last_session == 43 &&
              handler.last_option == "ascii_mode" &&
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

static void test_unavailable_private_server_stays_isolated() {
  WeaselTSF tsf;
  auto& client = WeaselTSFTestAccess::Client(tsf);
  const std::wstring name = weasel::GetPipeName() +
                            L"_UnavailablePrivateTest_" +
                            std::to_wstring(GetCurrentProcessId());
  TestChannelAccess::SetName(weasel::ClientTestAccess::Channel(client), name);
  const ULONGLONG started = GetTickCount64();
  check(!tsf._EnsureServerConnected(),
        "missing private server fails recovery without launching the real "
        "service");
  check(GetTickCount64() - started < 250,
        "missing server recovery returns promptly instead of stalling input");
  check(!client.IsSessionActive(),
        "missing private server leaves the test client inactive");
}

static void test_client_lifecycle_timeout_budgets() {
  const std::wstring name = weasel::GetPipeName() + L"_LifecycleTimeoutTest_" +
                            std::to_wstring(GetCurrentProcessId());
  weasel::PipeServer server{std::wstring(name)};
  auto handler = [](weasel::PipeMessage message,
                    weasel::PipeServer::Respond respond) {
    if (message.Msg == WEASEL_IPC_START_MAINTENANCE ||
        message.Msg == WEASEL_IPC_END_MAINTENANCE)
      Sleep(1250);
    else if (message.Msg == WEASEL_IPC_END_SESSION)
      Sleep(600);
    respond(message.Msg == WEASEL_IPC_START_SESSION ? 42 : 1);
  };
  boost::thread listener([&] { server.Listen(handler); });
  weasel::Client client;
  TestChannelAccess::SetName(weasel::ClientTestAccess::Channel(client), name);
  check(connect_with_retry(client), "lifecycle: isolated client connects");
  check(client.StartMaintenance(),
        "lifecycle: maintenance may take longer than the input timeout");
  ULONGLONG started = GetTickCount64();
  client.EndMaintenance();
  check(GetTickCount64() - started >= 1000 &&
            weasel::ClientTestAccess::Channel(client).Connected(),
        "lifecycle: resume also uses the longer maintenance budget");
  client.StartSession();
  check(client.IsSessionActive(),
        "lifecycle: establish a session before teardown");
  started = GetTickCount64();
  client.Disconnect();
  check(GetTickCount64() - started < 500 && !client.IsSessionActive(),
        "lifecycle: slow EndSession cannot pin Disconnect or destruction");
  server.StopListener(listener);
  server.DrainWorkers();
}

static void test_maintenance_session_recovery() {
  SessionHandler handler;
  weasel::ServerImpl server;
  server.SetRequestHandler(&handler);
  std::wstring pipe_name = weasel::GetPipeName() + L"_SessionRecoveryTest_" +
                           std::to_wstring(GetCurrentProcessId());
  auto& server_channel = weasel::ServerImplTestAccess::Channel(server);
  TestChannelAccess::SetName(server_channel, pipe_name);
  std::atomic<int> echo_messages{0};
  std::atomic<int> key_messages{0};
  std::atomic<int> start_session_messages{0};
  weasel::ServerImplTestAccess::PipeThread(server) =
      std::make_unique<boost::thread>([&] {
        server_channel.Listen(
            [&](weasel::PipeMessage msg, weasel::PipeServer::Respond respond) {
              if (msg.Msg == WEASEL_IPC_ECHO)
                ++echo_messages;
              else if (msg.Msg == WEASEL_IPC_PROCESS_KEY_EVENT)
                ++key_messages;
              else if (msg.Msg == WEASEL_IPC_START_SESSION)
                ++start_session_messages;
              DWORD result = 0;
              {
                std::lock_guard guard(g_api_mutex);
                weasel::ServerImplTestAccess::HandlePipeMessage(
                    server, msg, [&](DWORD reply) { result = reply; });
              }
              respond(result);
            });
      });

  WeaselTSF tsf;
  auto& tsf_client = WeaselTSFTestAccess::Client(tsf);
  TestChannelAccess::SetName(weasel::ClientTestAccess::Channel(tsf_client),
                             pipe_name);
  weasel::Client deployer;
  TestChannelAccess::SetName(weasel::ClientTestAccess::Channel(deployer),
                             pipe_name);
  bool connected =
      connect_with_retry(tsf_client) && connect_with_retry(deployer);
  check(connected, "TSF and deployer connect to the isolated server");
  if (!connected)
    return;

  tsf_client.StartSession();
  DWORD original_session = weasel::ClientTestAccess::SessionId(tsf_client);
  check(original_session != 0, "TSF establishes its initial session");

  int echo_before = handler.echo_calls;
  int echo_messages_before = echo_messages;
  check(tsf._EnsureServerConnected(), "valid session remains usable");
  check(handler.echo_calls == echo_before &&
            echo_messages == echo_messages_before &&
            handler.sessions_created == 1,
        "healthy connection check stays local and sends no Echo IPC");

  int key_before = key_messages;
  int accepted_before = handler.accepted_keys;
  int invalid_before = handler.invalid_keys;
  int start_before = start_session_messages;
  check(WeaselTSFTestAccess::ProcessKeyEventWithRecovery(tsf,
                                                         weasel::KeyEvent('a')),
        "healthy key is accepted");
  check(key_messages == key_before + 1 &&
            handler.accepted_keys == accepted_before + 1 &&
            handler.invalid_keys == invalid_before &&
            start_session_messages == start_before &&
            echo_messages == echo_messages_before,
        "healthy key uses exactly one key IPC and no Echo");

  // A different client performs deployment while the TSF pipe stays open.
  check(deployer.StartMaintenance(),
        "deployer receives maintenance acknowledgement after transition");
  check(handler.maintaining,
        "maintenance acknowledgement is sent only after handler transition");
  deployer.EndMaintenance();
  check(!handler.maintaining, "end-maintenance leaves the handler active");
  check(tsf_client.IsSessionActive(),
        "TSF keeps the stale cached session after maintenance");
  check(tsf._EnsureServerConnected(),
        "cached session check stays local after maintenance");

  key_before = key_messages;
  accepted_before = handler.accepted_keys;
  invalid_before = handler.invalid_keys;
  start_before = start_session_messages;
  check(WeaselTSFTestAccess::ProcessKeyEventWithRecovery(tsf,
                                                         weasel::KeyEvent('b')),
        "first post-maintenance key recovers the stale session");
  check(weasel::ClientTestAccess::SessionId(tsf_client) != original_session &&
            handler.sessions_created == 2,
        "TSF replaces the invalid session without a focus switch or restart");
  check(key_messages == key_before + 2 &&
            start_session_messages == start_before + 1 &&
            echo_messages == echo_messages_before,
        "stale key uses one rejection, one session rebuild, and one retry");
  check(handler.accepted_keys == accepted_before + 1 &&
            handler.invalid_keys == invalid_before + 1,
        "recovered key reaches the input engine exactly once");

  // A request accepted by the engine whose reply exceeds the deadline is
  // ambiguous: TSF must consume it locally without rebuilding or replaying it,
  // otherwise the host could process the same physical key a second time.
  start_before = start_session_messages;
  accepted_before = handler.accepted_keys;
  handler.delay_key_reply = true;
  const ULONGLONG started = GetTickCount64();
  check(WeaselTSFTestAccess::ProcessKeyEventWithRecovery(
            tsf, weasel::KeyEvent('c')),
        "ambiguous response timeout is consumed locally without replay");
  check(GetTickCount64() - started < 400,
        "TSF bounds a wedged healthy key transaction well below one second");
  check(start_session_messages == start_before && !tsf_client.IsSessionActive(),
        "response timeout does not rebuild the session");
  check(handler.accepted_keys == accepted_before + 1,
        "response timeout delivers the physical key at most once");
}

int main() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr))
    return 1;
  g_hInst = GetModuleHandle(nullptr);
  test_server_recovery_coordinator_state_machine();
  test_session_id_allocator_survives_maintenance_clear();
  test_pending_ui_generation_rejects_stale_extent();
  test_server_runtime_state_tracks_maintenance();
  test_pipe_ascii_commands();
  test_window_ascii_commands();
  test_unavailable_private_server_stays_isolated();
  test_client_lifecycle_timeout_budgets();
  test_maintenance_session_recovery();
  test_panel_recreate_reapplies_window_size();
  CoUninitialize();
  std::printf("Failures: %d\n", g_failures);
  return g_failures ? 1 : 0;
}
