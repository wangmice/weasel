#include "stdafx.h"

#include "Globals.h"
#include "Register.h"
#include "WeaselTSF.h"
#include <VersionHelpers.hpp>
#include <new>

class CClassFactory : public IClassFactory {
 public:
  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IClassFactory methods
  STDMETHODIMP CreateInstance(IUnknown* pUnkOuter,
                              REFIID riid,
                              void** ppvObject);
  STDMETHODIMP LockServer(BOOL fLock);
};

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == nullptr)
    return E_POINTER;
  *ppvObject = nullptr;
  if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown)) {
    *ppvObject = this;
    DllAddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CClassFactory::AddRef() {
  DllAddRef();
  return g_cRefDll + 1;
}

STDMETHODIMP_(ULONG) CClassFactory::Release() {
  DllRelease();
  return g_cRefDll + 1;
}

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter,
                                           REFIID riid,
                                           void** ppvObject) {
  WeaselTSF* pCase;
  HRESULT hr;
  if (ppvObject == nullptr)
    return E_POINTER;
  *ppvObject = nullptr;
  if (pUnkOuter != nullptr)
    return CLASS_E_NOAGGREGATION;
  if ((pCase = new WeaselTSF()) == nullptr)
    return E_OUTOFMEMORY;
  hr = pCase->QueryInterface(riid, ppvObject);
  pCase->Release();  // caller still holds ref if hr == S_OK
  return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock) {
  if (fLock)
    DllAddRef();
  else
    DllRelease();
  return S_OK;
}

static CClassFactory* g_classFactory = nullptr;

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject) {
  if (ppvObject == nullptr)
    return E_POINTER;
  *ppvObject = nullptr;
  if (!IsEqualCLSID(rclsid, c_clsidTextService))
    return CLASS_E_CLASSNOTAVAILABLE;

  EnterCriticalSection(&g_cs);
  if (g_classFactory == nullptr)
    g_classFactory = new (std::nothrow) CClassFactory();
  CClassFactory* factory = g_classFactory;
  LeaveCriticalSection(&g_cs);
  if (factory == nullptr)
    return E_OUTOFMEMORY;
  return factory->QueryInterface(riid, ppvObject);
}

STDAPI DllCanUnloadNow() {
  if (g_cRefDll >= 0)
    return S_FALSE;
  return S_OK;
}

STDAPI DllRegisterServer() {
  if (!RegisterServer() || !RegisterProfiles() || !RegisterCategories()) {
    DllUnregisterServer();
    return E_FAIL;
  }
  return S_OK;
}

STDAPI DllUnregisterServer() {
  UnregisterProfiles();
  UnregisterCategories();
  return UnregisterServer() ? S_OK : SELFREG_E_CLASS;
}
