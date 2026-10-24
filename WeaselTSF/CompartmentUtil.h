#pragma once

#include <msctf.h>

// Standalone compartment DWORD accessors, split from WeaselTSF so the
// lifetime rules (release only acquired pointers, truthful HRESULTs) are
// testable without a live text service.
HRESULT GetCompartmentDWORD(ITfThreadMgr* pThreadMgr,
                            TfClientId clientId,
                            DWORD& value,
                            const GUID guid);
HRESULT SetCompartmentDWORD(ITfThreadMgr* pThreadMgr,
                            TfClientId clientId,
                            const DWORD& value,
                            const GUID guid);
