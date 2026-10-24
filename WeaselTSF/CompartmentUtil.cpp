#include <Windows.h>
#include <atlcomcli.h>

#include "CompartmentUtil.h"

HRESULT GetCompartmentDWORD(ITfThreadMgr* pThreadMgr,
                            TfClientId clientId,
                            DWORD& value,
                            const GUID guid) {
  HRESULT hr = E_FAIL;
  CComPtr<ITfCompartmentMgr> pComMgr;
  hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&pComMgr);
  if (SUCCEEDED(hr)) {
    CComPtr<ITfCompartment> pCompartment;
    hr = pComMgr->GetCompartment(guid, &pCompartment);
    if (SUCCEEDED(hr)) {
      VARIANT var;
      hr = pCompartment->GetValue(&var);
      if (SUCCEEDED(hr)) {
        if (var.vt == VT_I4) {
          value = var.lVal;
          hr = S_OK;
        } else {
          hr = S_FALSE;
        }
      }
    }
  }
  return hr;
}

HRESULT SetCompartmentDWORD(ITfThreadMgr* pThreadMgr,
                            TfClientId clientId,
                            const DWORD& value,
                            const GUID guid) {
  HRESULT hr = E_FAIL;
  CComPtr<ITfCompartmentMgr> pComMgr;
  hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&pComMgr);
  if (SUCCEEDED(hr)) {
    CComPtr<ITfCompartment> pCompartment;
    hr = pComMgr->GetCompartment(guid, &pCompartment);
    if (SUCCEEDED(hr)) {
      VARIANT var;
      var.vt = VT_I4;
      var.lVal = value;
      hr = pCompartment->SetValue(clientId, &var);
    }
  }
  return hr;
}
