#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselTSF/Globals.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject);

namespace {

void Require(bool ok, const char* message) {
  if (ok)
    return;
  std::fprintf(stderr, "class factory regression: %s\n", message);
  std::abort();
}

}  // namespace

// Called once, after the test executable initializes g_cs and before any
// successful DllGetClassObject call, so the workers exercise cold publication.
void RunClassFactoryTests() {
  const CLSID foreign_clsid = {
      0x7f662924,
      0x4b1e,
      0x41f7,
      {0x96, 0x8f, 0x89, 0x30, 0x87, 0x29, 0xc1, 0x55}};

  void* object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
  const HRESULT foreign_hr =
      DllGetClassObject(foreign_clsid, IID_IClassFactory, &object);
  Require(foreign_hr == CLASS_E_CLASSNOTAVAILABLE,
          "foreign CLSID must be rejected");
  Require(object == nullptr, "foreign CLSID must clear the output pointer");

  const HRESULT null_output_hr =
      DllGetClassObject(c_clsidTextService, IID_IClassFactory, nullptr);
  Require(null_output_hr == E_POINTER,
          "null class-object output pointer must return E_POINTER");

  const LONG initial_refs = g_cRefDll;
  constexpr size_t kWorkers = 8;
  std::array<IClassFactory*, kWorkers> factories{};
  std::array<HRESULT, kWorkers> results{};
  std::mutex mutex;
  std::condition_variable cv;
  size_t ready = 0;
  bool go = false;
  std::vector<std::thread> workers;
  for (size_t i = 0; i < kWorkers; ++i) {
    workers.emplace_back([&, i] {
      {
        std::unique_lock<std::mutex> lock(mutex);
        ++ready;
        cv.notify_all();
        cv.wait(lock, [&] { return go; });
      }
      results[i] = DllGetClassObject(c_clsidTextService, IID_IClassFactory,
                                     reinterpret_cast<void**>(&factories[i]));
    });
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return ready == kWorkers; });
    go = true;
  }
  cv.notify_all();
  for (auto& worker : workers)
    worker.join();

  for (size_t i = 0; i < kWorkers; ++i) {
    Require(results[i] == S_OK && factories[i] != nullptr,
            "concurrent cold requests must return a class factory");
    Require(factories[i] == factories[0],
            "concurrent requests must share one factory");
  }
  Require(g_cRefDll == initial_refs + static_cast<LONG>(kWorkers),
          "each returned factory must hold one DLL reference");

  IClassFactory* factory = factories[0];
  IUnknown* unknown = nullptr;
  Require(DllGetClassObject(c_clsidTextService, IID_IUnknown,
                            reinterpret_cast<void**>(&unknown)) == S_OK &&
              unknown == static_cast<IUnknown*>(factory),
          "IID_IUnknown must return the same factory identity");
  Require(g_cRefDll == initial_refs + static_cast<LONG>(kWorkers) + 1,
          "IID_IUnknown must acquire a DLL reference");
  unknown->Release();

  object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
  Require(DllGetClassObject(c_clsidTextService, foreign_clsid, &object) ==
                  E_NOINTERFACE &&
              object == nullptr,
          "unsupported IID must return E_NOINTERFACE and clear the output");
  Require(factory->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
          "factory QueryInterface must reject a null output");
  Require(factory->CreateInstance(nullptr, IID_IUnknown, nullptr) == E_POINTER,
          "CreateInstance must reject a null output");
  object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
  Require(factory->CreateInstance(factory, IID_IUnknown, &object) ==
                  CLASS_E_NOAGGREGATION &&
              object == nullptr,
          "CreateInstance must reject aggregation and clear the output");
  Require(g_cRefDll == initial_refs + static_cast<LONG>(kWorkers),
          "failed requests must leave DLL references unchanged");

  factory->AddRef();
  Require(g_cRefDll == initial_refs + static_cast<LONG>(kWorkers) + 1,
          "factory AddRef must acquire a DLL reference");
  factory->Release();
  Require(factory->LockServer(TRUE) == S_OK &&
              g_cRefDll == initial_refs + static_cast<LONG>(kWorkers) + 1,
          "LockServer must acquire a DLL reference");
  Require(factory->LockServer(FALSE) == S_OK,
          "LockServer must release its DLL reference");
  for (auto* returned : factories)
    returned->Release();
  Require(g_cRefDll == initial_refs,
          "releasing all factories must restore the DLL reference count");
  std::puts(
      "[ok] class factory concurrency, IID, errors and reference balance");
}
