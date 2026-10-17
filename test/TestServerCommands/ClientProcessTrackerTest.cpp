#include "../../WeaselIPCServer/ClientProcessTracker.h"

#include <atomic>
#include <chrono>
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
            std::shared_ptr<LifetimeCounters> lifetime)
        : state_(std::move(state)), lifetime_(std::move(lifetime)) {}
    ~Process() override { ++lifetime_->processes_destroyed; }

    bool IsRunning() const override { return state_->running.load(); }
    std::unique_ptr<weasel::ClientProcessTracker::ExitWatch> WatchExit(
        std::function<void()> on_exit) override {
      {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->on_exit = std::move(on_exit);
      }
      return std::make_unique<Watch>(state_, lifetime_);
    }

   private:
    std::shared_ptr<State> state_;
    std::shared_ptr<LifetimeCounters> lifetime_;
  };

  std::unique_ptr<weasel::ClientProcessTracker::Process> Open(
      std::uint32_t pid) override {
    if (fail_open)
      return nullptr;
    auto it = states.find(pid);
    if (it == states.end())
      it = states.emplace(pid, std::make_shared<State>()).first;
    return std::make_unique<Process>(it->second, lifetime);
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

  void ReusePid(std::uint32_t pid) {
    states[pid] = std::make_shared<State>();
  }

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
  std::map<std::uint32_t, std::shared_ptr<State>> states;
  std::shared_ptr<LifetimeCounters> lifetime =
      std::make_shared<LifetimeCounters>();
};

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
    require(exited.size() == 1 && exited.front() == *first &&
                !tracker.IsAlive(*first),
            "process exit reports exactly one dead token and IsAlive closes race");
    backend->Exit(100);
    require(exited.size() == 1, "duplicate exit notification is suppressed");
    require(backend->WaitForRetirement(1, 1),
            "exited process/watch resources retire without waiting for PID reuse or Stop");

    backend->ReusePid(100);
    const auto reused = tracker.Resolve(100);
    require(reused && *reused != *first && tracker.IsAlive(*reused),
            "PID reuse receives a new process-generation token");

    backend->fail_open = true;
    require(!tracker.Resolve(200), "OpenProcess failure is non-fatal and unowned");
    backend->fail_open = false;
    require(tracker.Resolve(200).has_value(),
            "failed resolution does not poison a later successful attempt");

    tracker.Stop();
    require(!tracker.Resolve(300), "Stop prevents new process registrations");
  }
};

ClientProcessTrackerSelfTest g_client_process_tracker_self_test;

}  // namespace
