#include <ResponseParser.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void require_thread_safe_static(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] ThreadSafeStaticInit: " << what << std::endl;
  std::abort();
}

struct ThreadSafeStaticInitSelfTest {
  ThreadSafeStaticInitSelfTest() {
    constexpr int kThreads = 32;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire))
          std::this_thread::yield();

        std::wstring commit;
        weasel::ResponseParser parser(&commit);
        if (parser.deserializers.find(L"action") == parser.deserializers.end())
          failures.fetch_add(1, std::memory_order_relaxed);
      });
    }

    while (ready.load(std::memory_order_acquire) != kThreads)
      std::this_thread::yield();
    go.store(true, std::memory_order_release);

    for (auto& thread : threads)
      thread.join();

    require_thread_safe_static(failures.load(std::memory_order_relaxed) == 0,
                               "concurrent first ResponseParser construction "
                               "initializes factories safely");
  }
};

ThreadSafeStaticInitSelfTest g_thread_safe_static_init_self_test;

}  // namespace
