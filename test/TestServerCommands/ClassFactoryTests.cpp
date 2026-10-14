#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselTSF/Globals.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject);

namespace {

void Require(bool ok, const char* message) {
  if (ok)
    return;
  std::fprintf(stderr, "class factory regression: %s\n", message);
  std::abort();
}

struct ClassFactoryRegressionGuard {
  ClassFactoryRegressionGuard() {
    const CLSID foreign_clsid = {
        0x7f662924,
        0x4b1e,
        0x41f7,
        {0x96, 0x8f, 0x89, 0x30, 0x87, 0x29, 0xc1, 0x55}};

    void* object = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    const HRESULT foreign_hr =
        DllGetClassObject(foreign_clsid, IID_IClassFactory, &object);
    Require(foreign_hr == CLASS_E_CLASSNOTAVAILABLE,
            "foreign CLSID must be rejected");
    Require(object == nullptr,
            "foreign CLSID must clear the output pointer");

    const HRESULT null_output_hr =
        DllGetClassObject(c_clsidTextService, IID_IClassFactory, nullptr);
    Require(null_output_hr == E_POINTER,
            "null class-object output pointer must return E_POINTER");
  }
};

ClassFactoryRegressionGuard g_class_factory_regression_guard;

}  // namespace
