#include <windows.h>

#include "../../WeaselUI/d2d.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace weasel;

namespace {
int g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cout << "[FAIL] " << what << std::endl;
  } else {
    std::cout << "[ok] " << what << std::endl;
  }
}

struct ComScope {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~ComScope() {
    if (SUCCEEDED(hr))
      CoUninitialize();
  }
  bool ready() const { return SUCCEEDED(hr); }
};

bool valid_snapshot(const std::shared_ptr<const DeviceResourcesSnapshot>& s) {
  return s && s->generation != 0 && s->direct3dDevice && s->dxgiDevice &&
         s->dxFactory && s->d2Factory && s->d2Device && s->m_pWriteFactory;
}

void test_shared_generation_lifetime() {
  ComScope com;
  check(com.ready(), "COM initialized for shared resource lifetime test");
  if (!com.ready())
    return;

  auto& resources = DeviceResources::Get();
  check(!resources.HasLiveGeneration(),
        "no shared generation exists before the first lease");

  std::shared_ptr<const DeviceResourcesSnapshot> first;
  std::shared_ptr<const DeviceResourcesSnapshot> second;
  check(SUCCEEDED(resources.Acquire(first)) && valid_snapshot(first),
        "first lease acquires a complete shared generation");
  check(SUCCEEDED(resources.Acquire(second)) && valid_snapshot(second),
        "second lease acquires a complete shared generation");
  check(first.get() == second.get(),
        "concurrent leases share one immutable generation");

  Microsoft::WRL::ComPtr<ID2D1Multithread> multithread;
  check(SUCCEEDED(first->d2Factory.As(&multithread)) && multithread &&
            multithread->GetMultithreadProtected(),
        "shared Direct2D factory is multithread protected");
  multithread.Reset();

  first.reset();
  check(resources.HasLiveGeneration(),
        "generation remains alive while one lease remains");
  second.reset();
  check(!resources.HasLiveGeneration(),
        "last lease releases the shared generation");
}

void test_generation_rollover_preserves_old_snapshot() {
  ComScope com;
  check(com.ready(), "COM initialized for generation rollover test");
  if (!com.ready())
    return;

  auto& resources = DeviceResources::Get();
  std::shared_ptr<const DeviceResourcesSnapshot> first;
  std::shared_ptr<const DeviceResourcesSnapshot> replacement;
  std::shared_ptr<const DeviceResourcesSnapshot> stale_retry;

  check(SUCCEEDED(resources.Acquire(first)) && valid_snapshot(first),
        "initial generation acquired before simulated device loss");
  const uint64_t failed_generation = first ? first->generation : 0;

  check(SUCCEEDED(resources.RecreateAfterDeviceLoss(failed_generation,
                                                     replacement)) &&
            valid_snapshot(replacement),
        "device loss publishes a fresh complete generation");
  check(first && replacement &&
            replacement->generation != first->generation &&
            replacement.get() != first.get(),
        "replacement generation differs from failed generation");
  check(valid_snapshot(first),
        "old snapshot stays alive until its owner releases it");

  check(SUCCEEDED(resources.RecreateAfterDeviceLoss(failed_generation,
                                                     stale_retry)) &&
            stale_retry.get() == replacement.get(),
        "stale device-loss retry adopts the already-published generation");

  first.reset();
  replacement.reset();
  stale_retry.reset();
  check(!resources.HasLiveGeneration(),
        "rollover generations release after the last lease leaves");
}

void test_concurrent_acquire_publishes_one_generation() {
  constexpr int kThreads = 8;
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::shared_ptr<const DeviceResourcesSnapshot>> snapshots(
      kThreads);
  std::vector<HRESULT> results(kThreads, E_FAIL);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i]() {
      ComScope com;
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      if (com.ready())
        results[i] = DeviceResources::Get().Acquire(snapshots[i]);
    });
  }

  while (ready.load(std::memory_order_acquire) != kThreads)
    std::this_thread::yield();
  go.store(true, std::memory_order_release);
  for (auto& thread : threads)
    thread.join();

  const auto* expected = snapshots[0].get();
  bool all_same = expected != nullptr;
  for (int i = 0; i < kThreads; ++i) {
    all_same = all_same && SUCCEEDED(results[i]) &&
               valid_snapshot(snapshots[i]) && snapshots[i].get() == expected;
  }
  check(all_same, "parallel acquires observe one published generation");

  snapshots.clear();
  check(!DeviceResources::Get().HasLiveGeneration(),
        "parallel leases release the generation when all owners leave");
}
}  // namespace

int main() {
  test_shared_generation_lifetime();
  test_generation_rollover_preserves_old_snapshot();
  test_concurrent_acquire_publishes_one_generation();

  std::cout << (g_failures ? "FAILED" : "PASSED") << ": " << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
