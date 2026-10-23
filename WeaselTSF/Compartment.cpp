#include "stdafx.h"
#include "WeaselTSF.h"
#include "Compartment.h"
#include "CompartmentUtil.h"
#include "EditSession.h"
#include <resource.h>
#include <functional>
#include <ios>
#include <WeaselUtility.h>
#include "ResponseParser.h"
#include "CandidateList.h"
#include "LanguageBar.h"
#include <atomic>
#include <cstdint>

STDMETHODIMP CCompartmentEventSink::QueryInterface(REFIID riid,
                                                   _Outptr_ void** ppvObj) {
  if (ppvObj == nullptr)
    return E_INVALIDARG;

  *ppvObj = nullptr;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
    *ppvObj = (CCompartmentEventSink*)this;
  }

  if (*ppvObj) {
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCompartmentEventSink::AddRef() {
  return ++_refCount;
}

STDMETHODIMP_(ULONG) CCompartmentEventSink::Release() {
  LONG cr = --_refCount;

  assert(_refCount >= 0);

  if (_refCount == 0) {
    delete this;
  }

  return cr;
}

STDMETHODIMP CCompartmentEventSink::OnChange(_In_ REFGUID guidCompartment) {
  return _callback(guidCompartment);
}

HRESULT CCompartmentEventSink::_Advise(_In_ com_ptr<IUnknown> punk,
                                       _In_ REFGUID guidCompartment) {
  HRESULT hr = S_OK;
  ITfCompartmentMgr* pCompartmentMgr = nullptr;
  ITfSource* pSource = nullptr;

  hr = punk->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompartmentMgr);
  if (FAILED(hr)) {
    return hr;
  }

  hr = pCompartmentMgr->GetCompartment(guidCompartment, &_compartment);
  if (SUCCEEDED(hr)) {
    hr = _compartment->QueryInterface(IID_ITfSource, (void**)&pSource);
    if (SUCCEEDED(hr)) {
      hr = pSource->AdviseSink(IID_ITfCompartmentEventSink, this, &_cookie);
      pSource->Release();
    }
  }

  pCompartmentMgr->Release();

  return hr;
}
HRESULT CCompartmentEventSink::_Unadvise() {
  // Advise may have failed before _compartment was set: nothing is advised,
  // so there is nothing to unadvise.
  if (_compartment == nullptr)
    return S_FALSE;

  HRESULT hr = S_OK;
  ITfSource* pSource = nullptr;

  hr = _compartment->QueryInterface(IID_ITfSource, (void**)&pSource);
  if (SUCCEEDED(hr)) {
    hr = pSource->UnadviseSink(_cookie);
    pSource->Release();
  }

  _compartment = nullptr;
  _cookie = 0;

  return hr;
}

BOOL WeaselTSF::_IsKeyboardDisabled() {
  com_ptr<ITfDocumentMgr> pDocMgrFocus;
  com_ptr<ITfContext> pContext;

  // 与原实现一致：无焦点文档/无 top context 视为禁用（不缓存：瞬态）
  if ((_pThreadMgr->GetFocus(&pDocMgrFocus) != S_OK) || (pDocMgrFocus == NULL))
    return TRUE;
  if ((pDocMgrFocus->GetTop(&pContext) != S_OK) || (pContext == NULL))
    return TRUE;

  // 焦点 top context 变化（文档切换、push/pop）时重挂 sink 并使缓存失效
  if (pContext != _pDisabledCacheContext)
    _RetargetKeyboardDisabledSinks(pContext);

  // 命中缓存：sink 已挂在当前 context 上，且值未被事件置脏
  if ((_pDisabledCacheContext != nullptr) && !_fKeyboardDisabledDirty)
    return _fKeyboardDisabled;

  BOOL fDisabled = FALSE;
  com_ptr<ITfCompartmentMgr> pCompMgr;
  if (pContext->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompMgr) ==
      S_OK) {
    /* Check GUID_COMPARTMENT_KEYBOARD_DISABLED */
    {
      com_ptr<ITfCompartment> pCompartmentDisabled;
      if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED,
                                   &pCompartmentDisabled) == S_OK) {
        VARIANT var;
        if (pCompartmentDisabled->GetValue(&var) == S_OK) {
          if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
            fDisabled = (var.lVal != 0);
        }
      }
    }

    /* Check GUID_COMPARTMENT_EMPTYCONTEXT */
    {
      com_ptr<ITfCompartment> pCompartmentEmptyContext;
      if (pCompMgr->GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT,
                                   &pCompartmentEmptyContext) == S_OK) {
        VARIANT var;
        if (pCompartmentEmptyContext->GetValue(&var) == S_OK) {
          if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
            fDisabled = fDisabled || (var.lVal != 0);
        }
      }
    }
  }

  // 仅当 sink 就绪（能收到后续置脏事件）才缓存，否则逐键查询
  if (_pDisabledCacheContext != nullptr) {
    _fKeyboardDisabled = fDisabled;
    _fKeyboardDisabledDirty = FALSE;
  }
  return fDisabled;
}

void WeaselTSF::_RetargetKeyboardDisabledSinks(com_ptr<ITfContext> pContext) {
  using namespace std::placeholders;

  if (_pKeyboardDisabledSink)
    _pKeyboardDisabledSink->_Unadvise();
  if (_pEmptyContextSink)
    _pEmptyContextSink->_Unadvise();
  _pDisabledCacheContext = nullptr;
  _fKeyboardDisabledDirty = TRUE;

  if (pContext == nullptr)
    return;

  auto callback =
      std::bind(&WeaselTSF::_OnKeyboardDisabledCompartmentChange, this, _1);
  if (!_pKeyboardDisabledSink)
    _pKeyboardDisabledSink.Attach(new CCompartmentEventSink(callback));
  if (!_pEmptyContextSink)
    _pEmptyContextSink.Attach(new CCompartmentEventSink(callback));

  HRESULT hrDisabled = _pKeyboardDisabledSink->_Advise(
      (IUnknown*)pContext, GUID_COMPARTMENT_KEYBOARD_DISABLED);
  HRESULT hrEmpty = _pEmptyContextSink->_Advise((IUnknown*)pContext,
                                                GUID_COMPARTMENT_EMPTYCONTEXT);
  if (SUCCEEDED(hrDisabled) && SUCCEEDED(hrEmpty)) {
    _pDisabledCacheContext = pContext;
  }
  // advise 失败：_pDisabledCacheContext 保持空，_IsKeyboardDisabled
  // 不缓存、逐键全量查询，行为与原实现一致
}

HRESULT WeaselTSF::_OnKeyboardDisabledCompartmentChange(
    REFGUID guidCompartment) {
  _fKeyboardDisabledDirty = TRUE;
  return S_OK;
}

BOOL WeaselTSF::_IsKeyboardOpen() {
  com_ptr<ITfCompartmentMgr> pCompMgr;
  BOOL fOpen = FALSE;

  if (_pThreadMgr->QueryInterface(&pCompMgr) == S_OK) {
    com_ptr<ITfCompartment> pCompartment;
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                 &pCompartment) == S_OK) {
      VARIANT var;
      if (pCompartment->GetValue(&var) == S_OK) {
        if (var.vt == VT_I4)  // Even VT_EMPTY, GetValue() can succeed
          fOpen = (BOOL)var.lVal;
      }
    }
  }
  return fOpen;
}

HRESULT WeaselTSF::_SetKeyboardOpen(BOOL fOpen) {
  HRESULT hr = E_FAIL;
  com_ptr<ITfCompartmentMgr> pCompMgr;

  if (_pThreadMgr->QueryInterface(&pCompMgr) == S_OK) {
    com_ptr<ITfCompartment> pCompartment;
    if (pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                 &pCompartment) == S_OK) {
      VARIANT var;
      var.vt = VT_I4;
      var.lVal = fOpen;
      hr = pCompartment->SetValue(_tfClientId, &var);
    }
  }

  return hr;
}

namespace {
// A host may accept an async session but never run it after context disposal.
// Permit retry on subsequent notifications without flooding its TSF queue.
constexpr ULONGLONG kKeyboardOpenPendingRetryMs = 2000;
constexpr UINT kKeyboardOpenRetryTimer = 1;
constexpr UINT kKeyboardOpenCommitMessage = WM_APP + 0x3a0;
constexpr unsigned int kKeyboardOpenMaxRetries = 3;
constexpr UINT kKeyboardOpenRetryDelaysMs[] = {20, 50, 100};
std::atomic_uint64_t g_keyboardWindowClassSerial{0};

// TF_ES_ASYNCDONTCARE may execute synchronously within OnChange, while
// SetValue on that compartment is still forbidden (E_UNEXPECTED). Force an
// asynchronous session and reject callbacks from a previous TIP activation.
class CForceKeyboardOpenEditSession : public CEditSession {
 public:
  CForceKeyboardOpenEditSession(com_ptr<WeaselTSF> pTextService,
                                com_ptr<ITfContext> pContext,
                                UINT64 generation,
                                UINT64 requestSerial)
      : CEditSession(pTextService, pContext),
        generation_(generation),
        requestSerial_(requestSerial) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) {
    return _pTextService->_ApplyDeferredKeyboardOpen(generation_,
                                                     requestSerial_, _pContext);
  }

 private:
  UINT64 generation_;
  UINT64 requestSerial_;
};
}  // namespace

LRESULT CALLBACK WeaselTSF::_KeyboardOpenWindowProc(HWND hwnd,
                                                    UINT message,
                                                    WPARAM wParam,
                                                    LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }

  auto* self =
      reinterpret_cast<WeaselTSF*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && (message == WM_TIMER || message == kKeyboardOpenCommitMessage)) {
    // A host callback can reenter Deactivate while the command is in flight.
    self->AddRef();
    if (message == WM_TIMER) {
      if (wParam == kKeyboardOpenRetryTimer)
        self->_OnKeyboardOpenTimer();
    } else {
      self->_CommitDeferredKeyboardOpen();
    }
    self->Release();
    return 0;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool WeaselTSF::_EnsureKeyboardOpenWindow() {
  if (_keyboardOpenWindow)
    return true;

  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(&_KeyboardOpenWindowProc),
                          &module))
    return false;

  const auto serial =
      g_keyboardWindowClassSerial.fetch_add(1, std::memory_order_relaxed) + 1;
  _keyboardOpenWindowClassName =
      L"WeaselTSF.KeyboardOpen." +
      std::to_wstring(reinterpret_cast<std::uintptr_t>(module)) + L"." +
      std::to_wstring(serial);

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = _KeyboardOpenWindowProc;
  window_class.hInstance = module;
  window_class.lpszClassName = _keyboardOpenWindowClassName.c_str();
  const ATOM atom = RegisterClassW(&window_class);
  if (!atom) {
    _keyboardOpenWindowClassName.clear();
    FreeLibrary(module);
    return false;
  }

  const HWND window = CreateWindowExW(0, MAKEINTATOM(atom), L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, module, this);
  if (!window) {
    if (UnregisterClassW(MAKEINTATOM(atom), module))
      FreeLibrary(module);
    // On unregister failure keep the loader reference: the registered WNDPROC
    // must never outlive the DLL that owns it.
    _keyboardOpenWindowClassName.clear();
    return false;
  }

  _keyboardOpenWindow = window;
  _keyboardOpenWindowClassAtom = atom;
  _keyboardOpenWindowModule = module;
  return true;
}

void WeaselTSF::_CancelKeyboardOpenRequest() noexcept {
  ++_keyboardOpenRequestSerial;
  _keyboardOpenPending = false;
  _keyboardOpenPendingSinceTick = 0;
  _keyboardOpenRetries = 0;
  _keyboardOpenCommitPending = false;
  _keyboardOpenRequestContext = nullptr;
  if (_keyboardOpenWindow)
    KillTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer);
}

void WeaselTSF::_DestroyKeyboardOpenWindow() noexcept {
  _CancelKeyboardOpenRequest();
  if (_keyboardOpenWindow) {
    if (IsWindow(_keyboardOpenWindow) && !DestroyWindow(_keyboardOpenWindow)) {
      // Never leave a live WNDPROC pointing to a deactivated TIP.
      SetWindowLongPtrW(_keyboardOpenWindow, GWLP_USERDATA, 0);
      // The surviving window/class must also retain their DLL loader lease.
      _keyboardOpenWindow = nullptr;
      return;
    }
    _keyboardOpenWindow = nullptr;
  }
  if (_keyboardOpenWindowClassAtom) {
    if (!UnregisterClassW(MAKEINTATOM(_keyboardOpenWindowClassAtom),
                          _keyboardOpenWindowModule))
      return;  // Keep the DLL loaded while its WNDPROC class still exists.
    _keyboardOpenWindowClassAtom = 0;
  }
  _keyboardOpenWindowClassName.clear();
  if (_keyboardOpenWindowModule) {
    FreeLibrary(_keyboardOpenWindowModule);
    _keyboardOpenWindowModule = nullptr;
  }
}

bool WeaselTSF::_ScheduleKeyboardOpenRetry() noexcept {
  if (!_keyboardOpenWindow || !_keyboardOpenRequestContext || _isToOpenClose ||
      _keyboardOpenRetries >= kKeyboardOpenMaxRetries) {
    _CancelKeyboardOpenRequest();
    return false;
  }

  // The old edit session may still execute after its watchdog expires.
  // Invalidate it before another session is queued.
  ++_keyboardOpenRequestSerial;
  _keyboardOpenPending = false;
  _keyboardOpenPendingSinceTick = 0;
  const UINT delay = kKeyboardOpenRetryDelaysMs[_keyboardOpenRetries++];
  if (!SetTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer, delay, nullptr)) {
    _CancelKeyboardOpenRequest();
    return false;
  }
  return true;
}

void WeaselTSF::_OnKeyboardOpenTimer() {
  if (!_keyboardOpenWindow)
    return;
  KillTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer);
  if (_keyboardOpenCommitPending || !_keyboardOpenRequestContext)
    return;

  // A host can accept an asynchronous edit session and never invoke it.
  // Queue the retry after the normal backoff; never run a second edit session
  // directly inside the watchdog callback.
  if (_keyboardOpenPending) {
    // KillTimer does not retract an already-queued WM_TIMER. Ignore any
    // leftover timer from a previous request until this lease really expires.
    const ULONGLONG elapsed =
        GetTickCount64() - _keyboardOpenPendingSinceTick;
    if (elapsed < kKeyboardOpenPendingRetryMs) {
      const UINT remaining =
          static_cast<UINT>(kKeyboardOpenPendingRetryMs - elapsed);
      if (!SetTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer, remaining,
                    nullptr))
        _CancelKeyboardOpenRequest();
      return;
    }
    _ScheduleKeyboardOpenRetry();
    return;
  }
  if (_isToOpenClose || !_pThreadMgr) {
    _CancelKeyboardOpenRequest();
    return;
  }

  com_ptr<ITfDocumentMgr> document;
  com_ptr<ITfContext> focused;
  if (_pThreadMgr->GetFocus(&document) != S_OK || !document ||
      document->GetTop(&focused) != S_OK ||
      focused != _keyboardOpenRequestContext || _IsKeyboardOpen()) {
    _CancelKeyboardOpenRequest();
    return;
  }

  const HRESULT result = _RequestKeyboardOpenDeferred();
  if (FAILED(result)) {
    LOG(WARNING) << "Deferred keyboard recovery retry failed, hr: "
                 << static_cast<long>(result);
  }
}

void WeaselTSF::_CommitDeferredKeyboardOpen() {
  if (!_keyboardOpenCommitPending || !_keyboardOpenRequestContext)
    return;
  if (!_pThreadMgr || _isToOpenClose) {
    _CancelKeyboardOpenRequest();
    return;
  }
  com_ptr<ITfDocumentMgr> document;
  com_ptr<ITfContext> focused;
  if (_pThreadMgr->GetFocus(&document) != S_OK || !document ||
      document->GetTop(&focused) != S_OK ||
      focused != _keyboardOpenRequestContext || !_IsKeyboardOpen()) {
    _CancelKeyboardOpenRequest();
    return;
  }

  // Commit exactly once, outside the TSF edit session. Reset before IPC so
  // reentrant focus/compartment callbacks cannot replay an old command.
  _CancelKeyboardOpenRequest();
  _status.ascii_mode = !_status.ascii_mode;
  if (_pLangBarButton && _pLangBarButton->IsLangBarDisabled())
    _EnableLanguageBar(true);
  _HandleLangBarMenuSelect(_status.ascii_mode ? ID_WEASELTRAY_ENABLE_ASCII
                                              : ID_WEASELTRAY_DISABLE_ASCII);
  if (_pEditSessionContext)
    m_client.ClearComposition();
  _UpdateLanguageBar(_status);
}

HRESULT WeaselTSF::_RequestKeyboardOpenDeferred() {
  if (!_pThreadMgr)
    return E_UNEXPECTED;
  if (_keyboardOpenPending || _keyboardOpenCommitPending)
    return S_FALSE;

  // A private message-only window supplies just the post-edit-session
  // completion and bounded retry timer; TSF still owns edit-session ordering.
  if (!_EnsureKeyboardOpenWindow())
    return E_FAIL;

  com_ptr<ITfDocumentMgr> document;
  com_ptr<ITfContext> context;
  if (_pThreadMgr->GetFocus(&document) != S_OK || !document ||
      document->GetTop(&context) != S_OK || !context) {
    _CancelKeyboardOpenRequest();
    return E_FAIL;
  }
  if (_keyboardOpenRequestContext && _keyboardOpenRequestContext != context) {
    _CancelKeyboardOpenRequest();
    return S_FALSE;
  }
  _keyboardOpenRequestContext = context;

  const UINT64 generation = _keyboardOpenGeneration;
  const UINT64 serial = ++_keyboardOpenRequestSerial;
  com_ptr<CForceKeyboardOpenEditSession> session;
  session.Attach(
      new CForceKeyboardOpenEditSession(this, context, generation, serial));
  if (!session) {
    _CancelKeyboardOpenRequest();
    return E_OUTOFMEMORY;
  }

  _keyboardOpenPending = true;
  _keyboardOpenPendingSinceTick = GetTickCount64();
  if (!SetTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer,
                static_cast<UINT>(kKeyboardOpenPendingRetryMs), nullptr)) {
    _CancelKeyboardOpenRequest();
    return E_FAIL;
  }

  HRESULT hrSession = E_FAIL;
  const HRESULT hr = context->RequestEditSession(
      _tfClientId, session, TF_ES_ASYNC | TF_ES_READ, &hrSession);
  const HRESULT result = FAILED(hr) ? hr : hrSession;
  if (FAILED(result) && generation == _keyboardOpenGeneration &&
      serial == _keyboardOpenRequestSerial) {
    KillTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer);
    _keyboardOpenPending = false;
    _keyboardOpenPendingSinceTick = 0;
    _ScheduleKeyboardOpenRetry();
  }
  return result;
}

HRESULT WeaselTSF::_ApplyDeferredKeyboardOpen(UINT64 generation,
                                              UINT64 requestSerial,
                                              ITfContext* requestContext) {
  if (!_pThreadMgr || generation != _keyboardOpenGeneration ||
      !_keyboardOpenPending || requestSerial != _keyboardOpenRequestSerial)
    return S_FALSE;

  _keyboardOpenPending = false;
  _keyboardOpenPendingSinceTick = 0;
  if (_keyboardOpenWindow)
    KillTimer(_keyboardOpenWindow, kKeyboardOpenRetryTimer);

  if (_isToOpenClose) {
    _CancelKeyboardOpenRequest();
    return S_FALSE;
  }

  com_ptr<ITfDocumentMgr> document;
  com_ptr<ITfContext> focused;
  if (_pThreadMgr->GetFocus(&document) != S_OK || !document ||
      document->GetTop(&focused) != S_OK || !focused ||
      focused != requestContext || focused != _keyboardOpenRequestContext) {
    _CancelKeyboardOpenRequest();
    return S_FALSE;
  }
  if (_IsKeyboardOpen()) {
    _CancelKeyboardOpenRequest();
    return S_FALSE;
  }

  const BOOL priorSuppression = _fSuppressOpenCloseSelfWrite;
  _fSuppressOpenCloseSelfWrite = TRUE;
  const HRESULT hr = _SetKeyboardOpen(TRUE);
  _fSuppressOpenCloseSelfWrite = priorSuppression;
  if (FAILED(hr)) {
    LOG(ERROR) << "Deferred keyboard OPENCLOSE recovery failed, hr: "
               << static_cast<long>(hr);
    _ScheduleKeyboardOpenRetry();
    return hr;
  }

  // Never hold a TSF edit-session lock across synchronous pipe/COM UI work.
  _keyboardOpenCommitPending = true;
  if (!_keyboardOpenWindow ||
      !PostMessageW(_keyboardOpenWindow, kKeyboardOpenCommitMessage, 0, 0)) {
    _CancelKeyboardOpenRequest();
    return E_FAIL;
  }
  return S_OK;
}

HRESULT WeaselTSF::_GetCompartmentDWORD(DWORD& value, const GUID guid) {
  return GetCompartmentDWORD(_pThreadMgr, _tfClientId, value, guid);
}

HRESULT WeaselTSF::_SetCompartmentDWORD(const DWORD& value, const GUID guid) {
  return SetCompartmentDWORD(_pThreadMgr, _tfClientId, value, guid);
}

BOOL WeaselTSF::_InitCompartment() {
  using namespace std::placeholders;

  auto callback = std::bind(&WeaselTSF::_HandleCompartment, this, _1);
  _pKeyboardCompartmentSink.Attach(new CCompartmentEventSink(callback));
  if (!_pKeyboardCompartmentSink)
    return FALSE;
  HRESULT hrKeyboard = _pKeyboardCompartmentSink->_Advise(
      (IUnknown*)_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);

  _pConvertionCompartmentSink.Attach(new CCompartmentEventSink(callback));
  if (!_pConvertionCompartmentSink)
    return FALSE;
  HRESULT hrConversion = _pConvertionCompartmentSink->_Advise(
      (IUnknown*)_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);

  // Keep both results separately (the first used to be overwritten by the
  // second). On failure the sink stays around: _Unadvise() is null-safe, so
  // Deactivate() cleans it up when activation aborts.
  if (FAILED(hrKeyboard))
    LOG(ERROR) << "keyboard compartment advise failed: 0x" << std::hex
               << hrKeyboard;
  if (FAILED(hrConversion))
    LOG(ERROR) << "conversion compartment advise failed: 0x" << std::hex
               << hrConversion;
  return SUCCEEDED(hrKeyboard) && SUCCEEDED(hrConversion);
}

void WeaselTSF::_UninitCompartment() {
  if (_pKeyboardCompartmentSink) {
    _pKeyboardCompartmentSink->_Unadvise();
    _pKeyboardCompartmentSink = NULL;
  }
  if (_pConvertionCompartmentSink) {
    _pConvertionCompartmentSink->_Unadvise();
    _pConvertionCompartmentSink = NULL;
  }
  if (_pKeyboardDisabledSink) {
    _pKeyboardDisabledSink->_Unadvise();
    _pKeyboardDisabledSink = NULL;
  }
  if (_pEmptyContextSink) {
    _pEmptyContextSink->_Unadvise();
    _pEmptyContextSink = NULL;
  }
  _pDisabledCacheContext = NULL;
  _fKeyboardDisabledDirty = TRUE;
}

HRESULT WeaselTSF::_HandleCompartment(REFGUID guidCompartment) {
  if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
    if (_isToOpenClose) {
      BOOL isOpen = _IsKeyboardOpen();
      // clear composition when close keyboard
      if (!isOpen && _pEditSessionContext) {
        m_client.ClearComposition();
        _EndComposition(_pEditSessionContext, true);
      }
      _EnableLanguageBar(isOpen);
      _UpdateLanguageBar(_status);
    } else {
      // The self-triggered OnChange must not toggle ASCII mode again.
      if (_fSuppressOpenCloseSelfWrite)
        return S_OK;
      // Commit ASCII/Rime mode only in the genuinely asynchronous callback,
      // after SetValue succeeds. Never change mode for a failed request.
      const HRESULT hr = _RequestKeyboardOpenDeferred();
      if (FAILED(hr))
        LOG(ERROR) << "Could not queue keyboard OPENCLOSE recovery, hr: "
                   << static_cast<long>(hr);
    }
  } else if (IsEqualGUID(guidCompartment,
                         GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    BOOL isOpen = _IsKeyboardOpen();
    if (isOpen) {
      weasel::ResponseParser parser(NULL, NULL, &_status, NULL,
                                    &_cand->style());
      bool ok = m_client.GetResponseData(std::ref(parser));
      _UpdateLanguageBar(_status);
    }
  }
  return S_OK;
}