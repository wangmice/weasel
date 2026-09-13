// TestCompartmentUtil.cpp : regression tests for the compartment DWORD
// accessors (N2).
//
// Exercises the production free functions with a stub thread manager that
// injects GetCompartment/GetValue failures, and against a real in-process
// ITfThreadMgr. Guards the lifetime rules: only acquired pointers are
// released, and HRESULTs tell the truth.

#include <Windows.h>
#include <objbase.h>
#include <msctf.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#include "../../WeaselTSF/CompartmentUtil.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

// ===== stub COM objects =====
class StubCompartment : public ITfCompartment {
 public:
  VARIANT v{};
  bool failGetValue = false;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr)
      return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfCompartment) {
      *ppv = static_cast<ITfCompartment*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }  // stack object
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP SetValue(TfClientId, const VARIANT* pvar) override {
    v = *pvar;
    return S_OK;
  }
  STDMETHODIMP GetValue(VARIANT* pvar) override {
    if (failGetValue)
      return E_FAIL;
    *pvar = v;
    return S_OK;
  }
};

class StubCompartmentMgr : public ITfCompartmentMgr {
 public:
  StubCompartment comp;
  bool failGetCompartment = false;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr)
      return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfCompartmentMgr) {
      *ppv = static_cast<ITfCompartmentMgr*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP GetCompartment(REFGUID, ITfCompartment** pp) override {
    if (failGetCompartment)
      return E_FAIL;  // *pp never written
    *pp = &comp;
    comp.AddRef();
    return S_OK;
  }
  STDMETHODIMP ClearCompartment(TfClientId, REFGUID) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumCompartments(IEnumGUID**) override { return E_NOTIMPL; }
};

class StubThreadMgr : public ITfThreadMgr {
 public:
  StubCompartmentMgr mgr;
  bool failQi = false;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr)
      return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_ITfThreadMgr) {
      *ppv = static_cast<ITfThreadMgr*>(this);
      AddRef();
      return S_OK;
    }
    if (riid == IID_ITfCompartmentMgr && !failQi) {
      *ppv = static_cast<ITfCompartmentMgr*>(&mgr);
      mgr.AddRef();
      return S_OK;
    }
    *ppv = nullptr;  // *ppv never assigned to the compartment manager
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP Activate(TfClientId*) override { return E_NOTIMPL; }
  STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetFocus(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override {
    return E_NOTIMPL;
  }
};

static const GUID kGuid = {
    0x1cbd126d, 0x3f0e, 0x43af,
    {0xa1, 0x1b, 0x76, 0xf6, 0xb5, 0x5a, 0x11, 0x4d}};  // scratch GUID

int main() {
  HRESULT hr;
  StubThreadMgr stub;

  // failure paths must return cleanly (N2: used to Release a never-assigned
  // pointer and/or report a lying S_OK)
  stub.mgr.failGetCompartment = true;
  DWORD v = 0;
  check(GetCompartmentDWORD(&stub, 0, v, kGuid) == E_FAIL,
        "get: GetCompartment failure -> E_FAIL, no crash");
  check(SetCompartmentDWORD(&stub, 0, v, kGuid) == E_FAIL,
        "set: GetCompartment failure -> E_FAIL, no crash (was lying S_OK)");

  stub.mgr.failGetCompartment = false;
  stub.failQi = true;
  check(GetCompartmentDWORD(&stub, 0, v, kGuid) == E_NOINTERFACE,
        "get: QI failure -> E_NOINTERFACE, no crash");

  // happy path: VT_I4 read is S_OK (N2: used to return stale E_FAIL)
  stub.failQi = false;
  stub.mgr.comp.v.vt = VT_I4;
  stub.mgr.comp.v.lVal = 0x42;
  v = 0;
  hr = GetCompartmentDWORD(&stub, 0, v, kGuid);
  check(hr == S_OK && v == 0x42, "get: VT_I4 -> S_OK and value delivered");

  // VT_EMPTY is S_FALSE and leaves the out-value untouched
  stub.mgr.comp.v = {};
  v = 0x77;
  hr = GetCompartmentDWORD(&stub, 0, v, kGuid);
  check(hr == S_FALSE && v == 0x77, "get: VT_EMPTY -> S_FALSE, value kept");

  // GetValue failure propagates
  stub.mgr.comp.failGetValue = true;
  check(GetCompartmentDWORD(&stub, 0, v, kGuid) == E_FAIL,
        "get: GetValue failure -> E_FAIL");

  // round-trip against a real in-process ITfThreadMgr
  hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (SUCCEEDED(hr)) {
    ITfThreadMgr* ptm = nullptr;
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfThreadMgr, (void**)&ptm);
    if (SUCCEEDED(hr)) {
      TfClientId cid = 0;
      ptm->Activate(&cid);
      check(SetCompartmentDWORD(ptm, cid, 0x42,
                                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ==
                S_OK,
            "real msctf: set -> S_OK");
      DWORD got = 0;
      check(GetCompartmentDWORD(ptm, cid, got,
                                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ==
                S_OK &&
                got == 0x42,
            "real msctf: round-trip S_OK / 0x42");
      ptm->Deactivate();
      ptm->Release();
    } else {
      printf("[skip] real msctf unavailable (0x%08X)\n", (unsigned)hr);
    }
    CoUninitialize();
  }

  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
