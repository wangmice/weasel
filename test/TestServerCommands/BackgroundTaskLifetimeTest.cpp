#include "../../WeaselTSF/Globals.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

STDAPI DllCanUnloadNow();

namespace {

void require_background(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] BackgroundTaskLifetime: " << what << std::endl;
  std::abort();
}

struct BackgroundTaskLifetimeSelfTest {
  BackgroundTaskLifetimeSelfTest() {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::promise<void> body_finished;
    auto body_finished_future = body_finished.get_future();

    require_background(QueueDllBackgroundTask([&] {
                         entered.set_value();
                         release_future.wait();
                         body_finished.set_value();
                       }),
                       "queue a DLL-owned background task");
    require_background(entered_future.wait_for(std::chrono::seconds(2)) ==
                           std::future_status::ready,
                       "background task starts");
    require_background(
        DllCanUnloadNow() == S_FALSE,
        "DllCanUnloadNow stays false while background code runs");

    release.set_value();
    require_background(body_finished_future.wait_for(std::chrono::seconds(2)) ==
                           std::future_status::ready,
                       "background task body finishes");

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (DllCanUnloadNow() != S_OK &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require_background(DllCanUnloadNow() == S_OK,
                       "DLL logical reference is released after callback exit");
  }
};

BackgroundTaskLifetimeSelfTest g_background_task_lifetime_self_test;

}  // namespace
