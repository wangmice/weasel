#include "../../WeaselIPCServer/SessionRoutingHandler.h"
#include "TestClientPrivateAccess.h"

#include <PipeServer.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionLifecycleRace: " << what << std::endl;
  std::abort();
}

weasel::SessionKey Key(std::uint8_t tail) {
  weasel::SessionKey key{};
  key.bytes.back() = tail;
  return key;
}

class LifecycleChannelAccess : public weasel::PipeChannelBase {
 public:
  static void SetName(weasel::PipeChannelBase& channel,
                      const std::wstring& name) {
    channel.*&LifecycleChannelAccess::pname = name;
    channel.*&LifecycleChannelAccess::fallback_pname = std::wstring();
  }
};

class LifecycleBackend : public weasel::RequestHandler {
 public:
  DWORD AddSession(LPWSTR, EatLine eat) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const DWORD id = next_++;
    sessions_.insert(id);
    ++created_;
    if (eat) {
      std::wstring response = L"action=status\nstatus.ascii_mode=0\n.\n";
      eat(response);
    }
    return id;
  }

  DWORD FindSession(DWORD id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.count(id) ? id : 0;
  }

  bool RespondSession(DWORD id, EatLine eat) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sessions_.count(id))
      return false;
    if (eat) {
      std::wstring response = L"action=status\nstatus.ascii_mode=0\n.\n";
      eat(response);
    }
    return true;
  }

  DWORD RemoveSession(DWORD id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(id);
    return 0;
  }

  int Created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return created_;
  }

  std::size_t Live() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
  }

 private:
  mutable std::mutex mutex_;
  DWORD next_ = 100;
  int created_ = 0;
  std::set<DWORD> sessions_;
};

bool ConnectWithRetry(weasel::Client& client) {
  for (int waited = 0; waited < 2000; waited += 20) {
    if (client.Connect(100))
      return true;
    Sleep(20);
  }
  return false;
}

class LifecycleHarness {
 public:
  explicit LifecycleHarness(const wchar_t* suffix)
      : name_(weasel::GetPipeName() + suffix +
              std::to_wstring(GetCurrentProcessId())),
        server_(std::wstring(name_)) {
    router_.SetBackend(&backend_);
    listener_ = std::make_unique<boost::thread>([this] {
      server_.Listen([this](weasel::PipeMessage message,
                            weasel::PipeServer::Respond respond) {
        Handle(message, std::move(respond));
      });
    });
  }

  ~LifecycleHarness() {
    if (listener_) {
      server_.StopListener(*listener_);
      server_.DrainWorkers();
      listener_.reset();
    }
  }

  const std::wstring& Name() const { return name_; }
  LifecycleBackend& Backend() { return backend_; }

  void DropNextStartResponse() { drop_next_start_.store(true); }

  void DelayNextStartResponse(std::promise<void>& created,
                              std::shared_future<void> release) {
    created_ = &created;
    release_ = std::move(release);
    delay_next_start_.store(true);
  }

 private:
  void Handle(weasel::PipeMessage message, weasel::PipeServer::Respond respond) {
    DWORD result = 0;
    if (message.Msg == WEASEL_IPC_START_SESSION) {
      {
        std::lock_guard<std::mutex> lock(router_mutex_);
        result = router_.AddSession(
            reinterpret_cast<LPWSTR>(server_.ReceiveBuffer()),
            [this](std::wstring& body) -> bool {
              server_ << body;
              return true;
            });
      }

      if (delay_next_start_.exchange(false)) {
        if (created_)
          created_->set_value();
        if (release_.valid())
          release_.wait();
      }

      if (drop_next_start_.exchange(false))
        return;
      respond(result);
      return;
    }

    if (message.Msg == WEASEL_IPC_END_SESSION) {
      std::lock_guard<std::mutex> lock(router_mutex_);
      result = router_.RemoveSession(message.lParam);
      respond(result);
      return;
    }

    if (message.Msg == WEASEL_IPC_ECHO) {
      std::lock_guard<std::mutex> lock(router_mutex_);
      result = router_.FindSession(message.lParam);
      respond(result);
      return;
    }

    respond(0);
  }

  std::wstring name_;
  weasel::PipeServer server_;
  LifecycleBackend backend_;
  weasel::SessionRoutingHandler router_;
  std::mutex router_mutex_;
  std::unique_ptr<boost::thread> listener_;
  std::atomic<bool> drop_next_start_{false};
  std::atomic<bool> delay_next_start_{false};
  std::promise<void>* created_ = nullptr;
  std::shared_future<void> release_;
};

void TestLostStartResponseReusesSessionKey() {
  LifecycleHarness harness(L"_LostStartResponse_");
  weasel::Client client;
  LifecycleChannelAccess::SetName(weasel::ClientTestAccess::Channel(client),
                                  harness.Name());

  int generated = 0;
  weasel::ClientTestAccess::SetSessionKeyGenerator(
      client, [&]() -> std::optional<weasel::SessionKey> {
        ++generated;
        return Key(static_cast<std::uint8_t>(generated));
      });

  require(ConnectWithRetry(client), "lost-response client connects");
  harness.DropNextStartResponse();
  client.StartSession(150);
  require(!client.IsSessionActive(),
          "lost START response leaves the client without a routing id");
  require(harness.Backend().Created() == 1 && harness.Backend().Live() == 1,
          "server created exactly one session before the response was lost");
  require(generated == 1 &&
              weasel::ClientTestAccess::SessionKeyValue(client) == Key(1),
          "transport failure keeps the original logical SessionKey");

  client.Disconnect();
  require(weasel::ClientTestAccess::SessionKeyValue(client) == Key(1),
          "inactive disconnect used by recovery does not retire the key");
  require(ConnectWithRetry(client), "lost-response client reconnects");
  client.StartSession(1000);
  require(client.IsSessionActive() && generated == 1,
          "retry establishes the logical session with the original key");
  require(harness.Backend().Created() == 1 && harness.Backend().Live() == 1,
          "retry reuses the original backend session instead of orphaning it");

  client.Disconnect();
  require(harness.Backend().Live() == 0,
          "normal disconnect ends the recovered logical session");
}

void TestLateStartResponseCannotReviveRetiredGeneration() {
  LifecycleHarness harness(L"_LateStartResponse_");
  weasel::Client client;
  LifecycleChannelAccess::SetName(weasel::ClientTestAccess::Channel(client),
                                  harness.Name());

  std::atomic<int> generated{0};
  weasel::ClientTestAccess::SetSessionKeyGenerator(
      client, [&]() -> std::optional<weasel::SessionKey> {
        const int value = ++generated;
        return Key(static_cast<std::uint8_t>(value));
      });

  std::promise<void> created;
  auto created_future = created.get_future();
  std::promise<void> release;
  auto release_future = release.get_future().share();
  harness.DelayNextStartResponse(created, release_future);

  std::thread starter([&] {
    require(ConnectWithRetry(client), "racing START thread connects");
    client.StartSession(2000);
  });

  require(created_future.wait_for(std::chrono::seconds(2)) ==
              std::future_status::ready,
          "server creates the racing session before logical teardown");
  client.EndSession();
  release.set_value();
  starter.join();

  require(!client.IsSessionActive(),
          "late START response cannot publish a session after EndSession");
  require(!weasel::ClientTestAccess::SessionKeyValue(client).has_value(),
          "logical teardown keeps the retired generation retired");
  require(harness.Backend().Live() == 0,
          "late START result is cleaned up instead of becoming an orphan");

  require(ConnectWithRetry(client), "next logical generation connects");
  client.StartSession(1000);
  require(client.IsSessionActive() && generated.load() == 2,
          "next logical session uses a fresh generation and SessionKey");
  require(harness.Backend().Created() == 2 && harness.Backend().Live() == 1,
          "only the new generation remains live after the race");
  client.Disconnect();
}

struct SessionLifecycleRaceSelfTest {
  SessionLifecycleRaceSelfTest() {
    TestLostStartResponseReusesSessionKey();
    TestLateStartResponseCannotReviveRetiredGeneration();
  }
};

SessionLifecycleRaceSelfTest g_session_lifecycle_race_self_test;

}  // namespace
