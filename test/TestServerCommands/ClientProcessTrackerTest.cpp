#include "../../WeaselIPCServer/ClientProcessTracker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] ClientProcessTracker: " << what << std::endl;
  std::abort();
}

class FakeProcessBackend : public weasel::ClientProcessTracker::Backend {
 public:
  struct LifetimeCounters {
    std::atomic<int> processes_destroyed{0};
    std::atomic<int> watches_destroyed{0};
  };

  struct State {
    std::atomic<bool> running{true};
    std::mutex mutex;
    std::function<void()> on_exit;
  };

  // The fake process can remain inside WatchExit while another thread
  // invokes its exit callback. This deterministically exercises publication.
  struct RegistrationGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;

    bool WaitUntilEntered() {
      std::unique_lock<std::mutex> lock(mutex);
      return cv.wait_for(lock, std::chrono::seconds(2),
                         [this] { return entered; });
    }

    void Release() {
      {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
      }
      cv.notify_all();
    }
  };

  class Watch : public weasel::ClientProcessTracker::ExitWatch {
   public:
    Watch(std::shared_ptr<State> state,
          std::shared_ptr<LifetimeCounters> lifetime)
        : state_(std::move(state)), lifetime_(std::move(lifetime)) {}
    ~Watch() override {
      {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->on_exit = {};
      }
      ++lifetime_->watches_destroyed;
    }

   private:
    std::shared_ptr<State> state_;
    std::shared_ptr<LifetimeCounters> lifetime_;
  };

  class Process : public weasel::ClientProcessTracker::Process {
   public:
    Process(std::shared_ptr<State> state,
            std::shared_ptr<LifetimeCounters> lifetime,
            std::shared_ptr<RegistrationGate> gate)
        : state_(std::move(state)),
          lifetime_(std::move(lifetime)),
          registration_gate_(std::move(gate)) {}
    ~Process() override { ++lifetime_->processes_destroyed; }

    bool IsRunning() const override { return state_->running.load(); }
    std::unique_ptr<weasel::ClientProcessTracker::ExitWatch> WatchExit(
        std::function<void()> on_exit) override {
      {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->on_exit = std::move(on_exit);
      }
      if (registration_gate_) {
        std::unique_lock<std::mutex> lock(registration_gate_->mutex);
        registration_gate_->entered = true;
        registration_gate_->cv.notify_all();
        registration_gate_->cv.wait(
            lock, [this] { return registration_gate_->release; });
      }
      return std::make_unique<Watch>(state_, lifetime_);
    }

   private:
    std::shared_ptr<State> state_;
    std::shared_ptr<LifetimeCounters> lifetime_;
    std::shared_ptr<RegistrationGate> registration_gate_;
  };

  std::unique_ptr<weasel::ClientProcessTracker::Process> Open(
      std::uint32_t pid) override {
    if (fail_open)
      return nullptr;
    auto it = states.find(pid);
    if (it == states.end())
      it = states.emplace(pid, std::make_shared<State>()).first;
    return std::make_unique<Process>(it->second, lifetime, registration_gate);
  }

  void Exit(std::uint32_t pid) {
    auto state = states.at(pid);
    state->running.store(false);
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      callback = state->on_exit;
    }
    if (callback)
      callback();
  }

  void ReusePid(std::uint32_t pid) { states[pid] = std::make_shared<State>(); }

  bool WaitForRetirement(int process_count, int watch_count) const {
    for (int i = 0; i < 500; ++i) {
      if (lifetime->processes_destroyed.load() >= process_count &&
          lifetime->watches_destroyed.load() >= watch_count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  bool fail_open = false;
  std::shared_ptr<RegistrationGate> registration_gate;
  std::map<std::uint32_t, std::shared_ptr<State>> states;
  std::shared_ptr<LifetimeCounters> lifetime =
      std::make_shared<LifetimeCounters>();
};


// An exit notification can beat completion of registered-wait construction.
// No retirement may free Process until WatchExit returns and its watch is
// published. If the process died meanwhile, Resolve must not issue a token.
void TestExitDuringWatchRegistration() {
  auto backend = std::make_shared<FakeProcessBackend>();
  auto gate = std::make_shared<FakeProcessBackend::RegistrationGate>();
  backend->registration_gate = gate;
  std::atomic<int> exit_count{0};
  std::atomic<std::uint64_t> exited_token{0};
  weasel::ClientProcessTracker tracker(
      [&](weasel::ClientProcessToken token) {
        exited_token.store(token.value);
        ++exit_count;
      },
      backend);

  std::optional<weasel::ClientProcessToken> resolved;
  std::thread resolving([&] { resolved = tracker.Resolve(300); });
  require(gate->WaitUntilEntered(),
          "registration should reach the controlled WatchExit window");

  backend->Exit(300);
  require(exit_count.load() == 1 && exited_token.load() != 0,
          "early exit notifies its original process generation once");

  // Give the retirement worker an opportunity to drain the queue. The
  // registered Process and watch must still belong to Resolve at this point.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  require(backend->lifetime->processes_destroyed.load() == 0,
          "early callback must not destroy Process inside WatchExit");
  require(backend->lifetime->watches_destroyed.load() == 0,
          "early callback must not publish or destroy a partial watch");

  gate->Release();
  resolving.join();
  require(!resolved,
          "Resolve must reject a process that exited during registration");
  require(backend->WaitForRetirement(1, 1),
          "early-exit process and watch must eventually retire together");

  backend->registration_gate.reset();
  backend->ReusePid(300);
  auto reused = tracker.Resolve(300);
  require(reused && reused->value != exited_token.load() &&
              tracker.IsAlive(*reused),
          "PID reuse after early exit creates a distinct live token");
  tracker.Stop();
}

// Stop waits for the Resolve registration gate before destroying entries.
// It must not close the process handle while WatchExit uses that handle.
void TestStopDuringWatchRegistration() {
  auto backend = std::make_shared<FakeProcessBackend>();
  auto gate = std::make_shared<FakeProcessBackend::RegistrationGate>();
  backend->registration_gate = gate;
  std::atomic<int> exit_count{0};
  weasel::ClientProcessTracker tracker(
      [&](weasel::ClientProcessToken) { ++exit_count; }, backend);

  std::thread resolving([&] { tracker.Resolve(400); });
  require(gate->WaitUntilEntered(),
          "Stop race should reach the controlled WatchExit window");

  std::atomic<bool> stop_started{false};
  std::atomic<bool> stop_finished{false};
  std::thread stopping([&] {
    stop_started.store(true);
    tracker.Stop();
    stop_finished.store(true);
  });
  while (!stop_started.load())
    std::this_thread::yield();
  require(!stop_finished.load(),
          "Stop must not complete before pending registration is released");
  require(backend->lifetime->processes_destroyed.load() == 0,
          "Stop must not destroy Process during WatchExit");

  gate->Release();
  resolving.join();
  stopping.join();
  require(stop_finished.load() && exit_count.load() == 0,
          "Stop completes without an invented exit callback");
  require(backend->lifetime->processes_destroyed.load() == 1 &&
              backend->lifetime->watches_destroyed.load() == 1,
          "Stop unregisters the completed watch before destroying Process");
  require(!tracker.Resolve(400), "stopped tracker rejects new registrations");
}

struct ClientProcessTrackerSelfTest {
  ClientProcessTrackerSelfTest() {
    auto backend = std::make_shared<FakeProcessBackend>();
    std::vector<weasel::ClientProcessToken> exited;
    weasel::ClientProcessTracker tracker(
        [&](weasel::ClientProcessToken token) { exited.push_back(token); },
        backend);

    const auto first = tracker.Resolve(100);
    const auto same = tracker.Resolve(100);
    require(first && same && *first == *same && tracker.IsAlive(*first),
            "same live process resolves to one stable generation token");

    backend->Exit(100);
    require(
        exited.size() == 1 && exited.front() == *first &&
            !tracker.IsAlive(*first),
        "process exit reports exactly one dead token and IsAlive closes race");
    backend->Exit(100);
    require(exited.size() == 1, "duplicate exit notification is suppressed");
    require(backend->WaitForRetirement(1, 1),
            "exited process/watch resources retire without waiting for PID "
            "reuse or Stop");

    backend->ReusePid(100);
    const auto reused = tracker.Resolve(100);
    require(reused && *reused != *first && tracker.IsAlive(*reused),
            "PID reuse receives a new process-generation token");

    backend->fail_open = true;
    require(!tracker.Resolve(200),
            "OpenProcess failure is non-fatal and unowned");
    backend->fail_open = false;
    require(tracker.Resolve(200).has_value(),
            "failed resolution does not poison a later successful attempt");

    tracker.Stop();
    require(!tracker.Resolve(300), "Stop prevents new process registrations");

    TestExitDuringWatchRegistration();
    TestStopDuringWatchRegistration();
  }
};

ClientProcessTrackerSelfTest g_client_process_tracker_self_test;

}  // namespace
