#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"
#include "AsyncUiPolicy.h"
#include <WeaselInteractionPolicy.h>
#include <algorithm>

namespace {

weasel::tsf::ApplyFailureClass ClassifyApplyFailure(HRESULT hr) {
  if (hr == TF_E_DISCONNECTED || hr == TS_E_READONLY || hr == E_INVALIDARG ||
      hr == E_OUTOFMEMORY) {
    return weasel::tsf::ApplyFailureClass::Terminal;
  }
  return weasel::tsf::ApplyFailureClass::Retryable;
}

}  // namespace

/* Start Composition */
HRESULT WeaselTSF::_StartCompositionLocked(TfEditCookie ec,
                                           com_ptr<ITfContext> pContext,
                                           BOOL fCUASWorkaroundEnabled,
                                           UINT64 pendingUIGeneration) {
  UNREFERENCED_PARAMETER(fCUASWorkaroundEnabled);
  if (!pContext)
    return E_INVALIDARG;
  if (pendingUIGeneration != 0 &&
      !_IsPendingUIGenerationCurrent(pendingUIGeneration))
    return S_OK;

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  if (FAILED(pContext->QueryInterface(IID_ITfInsertAtSelection,
                                      (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  if (FAILED(pInsertAtSelection->InsertTextAtSelection(
          ec, TF_IAS_QUERYONLY, NULL, 0, &pRangeComposition)))
    return E_FAIL;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  if (FAILED(pContext->QueryInterface(IID_ITfContextComposition,
                                      (LPVOID*)&pContextComposition)))
    return E_FAIL;
  HRESULT hr = pContextComposition->StartComposition(ec, pRangeComposition,
                                                     this, &pComposition);
  if (FAILED(hr) || pComposition == nullptr)
    return FAILED(hr) ? hr : E_FAIL;

  const bool generationCurrent =
      pendingUIGeneration == 0 ||
      _IsPendingUIGenerationCurrent(pendingUIGeneration);
  if (!generationCurrent) {
    pComposition->EndComposition(ec);
    return S_OK;
  }

  hr = pRangeComposition->SetText(ec, 0, L"", 0);
  if (FAILED(hr)) {
    pComposition->EndComposition(ec);
    return hr;
  }

  _SetComposition(pComposition, pContext);
  _StartUI(pContext);

  TF_SELECTION tfSelection;
  pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  pContext->SetSelection(ec, 1, &tfSelection);
  return S_OK;
}

class CStartCompositionEditSession : public CEditSession {
 public:
  CStartCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                               com_ptr<ITfContext> pContext,
                               BOOL fCUASWorkaroundEnabled,
                               UINT64 pendingUIGeneration)
      : CEditSession(pTextService, pContext),
        _fCUASWorkaroundEnabled(fCUASWorkaroundEnabled),
        _pendingUIGeneration(pendingUIGeneration) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    const HRESULT hr = _pTextService->_StartCompositionLocked(
        ec, _pContext, _fCUASWorkaroundEnabled, _pendingUIGeneration);
    if (SUCCEEDED(hr) && _pTextService->_IsComposingInContext(_pContext))
      _pTextService->_UpdateCompositionWindow(_pContext, _pendingUIGeneration);
    return hr;
  }

 private:
  BOOL _fCUASWorkaroundEnabled;
  UINT64 _pendingUIGeneration;
};

void WeaselTSF::_StartComposition(com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled,
                                  UINT64 pendingUIGeneration) {
  com_ptr<CStartCompositionEditSession> pEditSession;
  pEditSession.Attach(new CStartCompositionEditSession(
      this, pContext, fCUASWorkaroundEnabled, pendingUIGeneration));
  if (!pEditSession)
    return;
  HRESULT hr = E_FAIL;
  pContext->RequestEditSession(_tfClientId, pEditSession,
                               TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
}

/* End Composition */
HRESULT WeaselTSF::_EndCompositionLocked(TfEditCookie ec,
                                         com_ptr<ITfContext> pContext,
                                         com_ptr<ITfComposition> pComposition,
                                         BOOL clear,
                                         BOOL endUI) {
  if (endUI)
    _EndUI();
  if (!pComposition || !pContext)
    return S_OK;

  _ClearCompositionDisplayAttributes(ec, pContext, pComposition);

  const bool wasCurrent = _IsCurrentComposition(pComposition);
  com_ptr<ITfRange> pCompositionRange;
  if (weasel::tsf::ShouldClearCompositionRange(clear, wasCurrent) &&
      SUCCEEDED(pComposition->GetRange(&pCompositionRange))) {
    const HRESULT clearHr = pCompositionRange->SetText(ec, 0, L"", 0);
    if (FAILED(clearHr))
      return clearHr;
  }

  if (wasCurrent)
    _FinalizeComposition();
  const HRESULT hr = pComposition->EndComposition(ec);
  if (FAILED(hr) && wasCurrent && !_IsComposing())
    _SetComposition(pComposition, pContext);
  return hr;
}

using ResponseApplyQueue = weasel::tsf::SerialApplyQueue<
    std::shared_ptr<WeaselTSF::ResponseApplyRequest>>;

class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(
      com_ptr<WeaselTSF> pTextService,
      com_ptr<ITfContext> pContext,
      com_ptr<ITfComposition> pComposition,
      BOOL clear,
      UINT64 pauseToken,
      ResponseApplyQueue* queue,
      std::shared_ptr<WeaselTSF::RetainedComposition> retained = {})
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _clear(clear),
        _pauseToken(pauseToken),
        _queue(queue),
        _retained(std::move(retained)) {}

  ~CEndCompositionEditSession() override {
    if (_retained && !_retainedExecuted)
      _retained->cleanup_pending = false;
    _ReleasePause();
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    if (_retained) {
      _retainedExecuted = true;
      _retained->cleanup_pending = false;
      return _pTextService->_CleanupRetainedCompositionLocked(ec, _retained);
    }
    const HRESULT hr = _pTextService->_EndCompositionLocked(
        ec, _pContext, _pComposition, _clear, FALSE);
    _ReleasePause();
    return hr;
  }

 private:
  void _ReleasePause() {
    if (_queue && _pauseToken != 0 && _queue->Resume(_pauseToken)) {
      _pauseToken = 0;
      _pTextService->_ScheduleNextResponseApply(false);
    }
  }

  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
  UINT64 _pauseToken;
  ResponseApplyQueue* _queue;
  std::shared_ptr<WeaselTSF::RetainedComposition> _retained;
  bool _retainedExecuted = false;
};

void WeaselTSF::_RetainComposition(com_ptr<ITfContext> context,
                                   com_ptr<ITfComposition> composition) {
  if (!_IsCurrentComposition(composition) || !_IsComposingInContext(context))
    return;
  auto retained = std::make_shared<RetainedComposition>();
  retained->context = context;
  retained->composition = composition;
  _retainedCompositions.push_back(std::move(retained));
  _InvalidatePendingUI();
  _FinalizeComposition();
  _EndUI();
}

bool WeaselTSF::_OwnsRetainedComposition(
    const std::shared_ptr<RetainedComposition>& retained) const {
  return retained &&
         std::find(_retainedCompositions.begin(), _retainedCompositions.end(),
                   retained) != _retainedCompositions.end();
}

bool WeaselTSF::_ForgetRetainedComposition(ITfComposition* composition) {
  const auto found =
      std::find_if(_retainedCompositions.begin(), _retainedCompositions.end(),
                   [composition](const auto& retained) {
                     return retained->composition == composition;
                   });
  if (found == _retainedCompositions.end())
    return false;
  _retainedCompositions.erase(found);
  return true;
}

bool WeaselTSF::_RestoreRetainedComposition(com_ptr<ITfContext> context) {
  if (_IsComposing())
    return false;
  const auto found =
      std::find_if(_retainedCompositions.begin(), _retainedCompositions.end(),
                   [&context](const auto& retained) {
                     return retained->context == context;
                   });
  if (found == _retainedCompositions.end())
    return false;
  const auto retained = *found;
  // Removing the owner invalidates every older cleanup callback before the
  // composition can be used to replace preedit with newly committed text.
  _retainedCompositions.erase(found);
  _SetComposition(retained->composition, context);
  return true;
}

void WeaselTSF::_ScheduleRetainedCompositionCleanup() {
  if (_tfClientId == TF_CLIENTID_NULL || _responseApplyQueue.InFlight() ||
      _responseApplyQueue.IsPaused())
    return;
  for (const auto& retained : _retainedCompositions) {
    if (retained->cleanup_pending ||
        _responseApplyQueue.AnyPending([&retained](const auto& request) {
          return request && request->context == retained->context;
        }))
      continue;
    // Keep a local owner across RequestEditSession and its reentrant callbacks.
    const auto owner = retained;
    owner->cleanup_pending = true;
    com_ptr<CEndCompositionEditSession> session;
    session.Attach(new CEndCompositionEditSession(this, owner->context,
                                                  owner->composition, TRUE, 0,
                                                  &_responseApplyQueue, owner));
    HRESULT sessionHr = E_FAIL;
    const HRESULT requestHr = owner->context->RequestEditSession(
        _tfClientId, session, TF_ES_ASYNC | TF_ES_READWRITE, &sessionHr);
    if (requestHr == TF_E_DISCONNECTED && _OwnsRetainedComposition(owner))
      _ForgetRetainedComposition(owner->composition);
    // At most one cleanup request per input; dropping an async request never
    // schedules another one from its COM destructor.
    return;
  }
}

HRESULT WeaselTSF::_CleanupRetainedCompositionLocked(
    TfEditCookie ec,
    const std::shared_ptr<RetainedComposition>& retained) {
  const auto mayClean = [&]() {
    return _OwnsRetainedComposition(retained) &&
           !_responseApplyQueue.AnyPending([&retained](const auto& request) {
             return request && request->context == retained->context;
           });
  };
  if (!mayClean() || !_responseApplyQueue.BeginBarrier())
    return S_FALSE;
  const UINT64 flight = _responseApplyQueue.FlightToken();
  const auto ownsFlight = [&]() {
    return _responseApplyQueue.InFlight() &&
           _responseApplyQueue.FlightToken() == flight;
  };
  const auto cleanup = [&]() -> HRESULT {
    com_ptr<ITfRange> range;
    HRESULT hr = retained->composition->GetRange(&range);
    if (FAILED(hr))
      return hr;
    if (!mayClean() || !ownsFlight())
      return S_FALSE;
    _ClearCompositionDisplayAttributes(ec, retained->context,
                                       retained->composition);
    if (!mayClean() || !ownsFlight())
      return S_FALSE;
    hr = range->SetText(ec, 0, L"", 0);
    if (FAILED(hr))
      return hr;
    if (!mayClean() || !ownsFlight())
      return S_FALSE;
    hr = retained->composition->EndComposition(ec);
    if (SUCCEEDED(hr) && _OwnsRetainedComposition(retained))
      _ForgetRetainedComposition(retained->composition);
    return hr;
  };
  const HRESULT hr = cleanup();
  if (_responseApplyQueue.Complete(flight))
    _ScheduleNextResponseApply(false);
  return hr;
}

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext,
                                BOOL clear,
                                BOOL endUI,
                                DWORD editSessionFlags) {
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;

  if (endUI)
    _EndUI();

  // An abandoned handoff exclusively owns its pause token. An unrelated
  // end-composition session cannot resume its queue barrier.
  const UINT64 pauseToken =
      _handoffWaitPauseToken == 0 && _responseApplyQueue.IsPaused()
          ? _responseApplyQueue.PauseToken()
          : 0;
  CEndCompositionEditSession* pEditSession = new CEndCompositionEditSession(
      this, pContext, pComposition, clear, pauseToken, &_responseApplyQueue);
  if (!pEditSession) {
    if (pauseToken != 0 && _responseApplyQueue.Resume(pauseToken))
      _ScheduleNextResponseApply(false);
    return;
  }

  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pEditSession, editSessionFlags | TF_ES_READWRITE,
      &sessionHr);

  if (requestHr == TF_E_DISCONNECTED && _IsCurrentComposition(pComposition))
    _FinalizeComposition();

  pEditSession->Release();
}

/* Get Text Extent */
class CGetTextExtentEditSession : public CEditSession {
 public:
  CGetTextExtentEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfContextView> pContextView,
                            com_ptr<ITfComposition> pComposition,
                            bool enhancedPosition,
                            UINT64 pendingUIGeneration)
      : CEditSession(pTextService, pContext),
        _pContextView(pContextView),
        _pComposition(pComposition),
        _enhancedPosition(enhancedPosition),
        _pendingUIGeneration(pendingUIGeneration) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override;

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  bool _enhancedPosition;
  UINT64 _pendingUIGeneration;
};

STDMETHODIMP CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) {
  if (_pendingUIGeneration != 0 &&
      !_pTextService->_IsPendingUIGenerationCurrent(_pendingUIGeneration))
    return S_OK;

  auto flushWithoutPosition = [&]() {
    if (_pendingUIGeneration != 0)
      _pTextService->_FlushPendingUI(_pendingUIGeneration);
  };

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  ITfRange* pRange = nullptr;
  RECT rc;
  BOOL fClipped;
  TF_SELECTION selection;
  ULONG nSelection = 0;

  if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                       (LPVOID*)&pInsertAtSelection))) {
    flushWithoutPosition();
    return E_FAIL;
  }
  if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                     &nSelection)) ||
      nSelection != 1 || selection.range == nullptr) {
    flushWithoutPosition();
    return E_FAIL;
  }

  if (_pComposition != nullptr && SUCCEEDED(_pComposition->GetRange(&pRange))) {
    pRange->Collapse(ec, TF_ANCHOR_START);
    selection.range->Release();
  } else {
    pRange = selection.range;
  }

  bool positioned = false;
  if (SUCCEEDED(_pContextView->GetTextExt(ec, pRange, &rc, &fClipped)) &&
      (rc.left != 0 || rc.top != 0)) {
    if (_enhancedPosition) {
      HWND hwnd = ::GetForegroundWindow();
      RECT rcForegroundWindow{};
      const bool hasForegroundWindow = hwnd != nullptr;
      const bool hasForegroundRect =
          hasForegroundWindow && ::GetWindowRect(hwnd, &rcForegroundWindow);

      if (weasel::interaction_policy::CanUseEnhancedForegroundBounds(
              _enhancedPosition, hasForegroundWindow, hasForegroundRect) &&
          (rc.left < rcForegroundWindow.left ||
           rc.left > rcForegroundWindow.right ||
           rc.top < rcForegroundWindow.top ||
           rc.top > rcForegroundWindow.bottom)) {
        POINT pt;
        bool hasCaret = ::GetCaretPos(&pt);
        int offsetx = rcForegroundWindow.left - rc.left + (hasCaret ? pt.x : 0);
        int offsety = rcForegroundWindow.top - rc.top + (hasCaret ? pt.y : 0);
        rc.left += offsetx;
        rc.right += offsetx;
        rc.top += offsety;
        rc.bottom += offsety;
      }
    }
    _pTextService->_SetCompositionPosition(rc, _pendingUIGeneration);
    positioned = true;
  }
  pRange->Release();
  if (!positioned)
    flushWithoutPosition();
  return S_OK;
}

BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext,
                                         UINT64 pendingUIGeneration) {
  if (pendingUIGeneration == 0 && _IsComposingInContext(pContext))
    pendingUIGeneration = _pendingUIGeneration;

  auto flushWithoutPosition = [&]() {
    if (pendingUIGeneration != 0)
      _FlushPendingUI(pendingUIGeneration);
  };

  com_ptr<ITfContextView> pContextView;
  if (!pContext || pContext->GetActiveView(&pContextView) != S_OK) {
    flushWithoutPosition();
    return FALSE;
  }

  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  pEditSession.Attach(new CGetTextExtentEditSession(
      this, pContext, pContextView, pComposition,
      _cand->style().enhanced_position, pendingUIGeneration));
  if (!pEditSession) {
    flushWithoutPosition();
    return FALSE;
  }

  HRESULT hr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
  if (FAILED(requestHr) || (hr != TF_S_ASYNC && FAILED(hr))) {
    flushWithoutPosition();
    return FALSE;
  }
  return TRUE;
}

void WeaselTSF::_SetCompositionPosition(const RECT& rc,
                                        UINT64 pendingUIGeneration) {
  if (pendingUIGeneration != 0 &&
      !_IsPendingUIGenerationCurrent(pendingUIGeneration))
    return;

  if (!_fCUASWorkaroundTested) {
    _fCUASWorkaroundTested = TRUE;
    if (rc.top == rc.bottom) {
      _fCUASWorkaroundEnabled = TRUE;
      if (pendingUIGeneration != 0)
        _FlushPendingUI(pendingUIGeneration);
      return;
    }
  }

  m_client.UpdateInputPosition(rc);
  _cand->UpdateInputPosition(rc);
  if (pendingUIGeneration != 0)
    _FlushPendingUI(pendingUIGeneration);
}

UINT64 WeaselTSF::_QueuePendingUI(
    const std::shared_ptr<weasel::Context>& context,
    const weasel::Status& status) {
  ++_pendingUIGeneration;
  if (_pendingUIGeneration == 0)
    ++_pendingUIGeneration;
  _pendingUIContext = context;
  _pendingUIStatus = status;
  _hasPendingUI = true;
  return _pendingUIGeneration;
}

void WeaselTSF::_InvalidatePendingUI() {
  ++_pendingUIGeneration;
  if (_pendingUIGeneration == 0)
    ++_pendingUIGeneration;
  _pendingUIContext.reset();
  _hasPendingUI = false;
}

bool WeaselTSF::_IsPendingUIGenerationCurrent(UINT64 generation) const {
  return generation != 0 && generation == _pendingUIGeneration &&
         _pendingUIContext != nullptr;
}

bool WeaselTSF::_TakePendingUI(UINT64 generation,
                               std::shared_ptr<weasel::Context>& context,
                               weasel::Status& status) {
  if (!_IsPendingUIGenerationCurrent(generation) || !_hasPendingUI)
    return false;
  context = _pendingUIContext;
  status = _pendingUIStatus;
  _hasPendingUI = false;
  return context != nullptr;
}

void WeaselTSF::_FlushPendingUI(UINT64 pendingUIGeneration) {
  std::shared_ptr<weasel::Context> context;
  weasel::Status status;
  if (!_TakePendingUI(pendingUIGeneration, context, status))
    return;
  _UpdateUI(*context, status);
}

/* Inline Preedit */
HRESULT WeaselTSF::_ShowInlinePreeditLocked(
    TfEditCookie ec,
    com_ptr<ITfContext> pContext,
    com_ptr<ITfComposition> pComposition,
    const std::shared_ptr<weasel::Context>& context,
    UINT64 pendingUIGeneration) {
  if (!pComposition || !context || !_IsCurrentComposition(pComposition) ||
      !_IsComposingInContext(pContext))
    return S_OK;
  if (pendingUIGeneration != 0 &&
      !_IsPendingUIGenerationCurrent(pendingUIGeneration))
    return S_OK;

  com_ptr<ITfRange> pRangeComposition;
  if (FAILED(pComposition->GetRange(&pRangeComposition)))
    return E_FAIL;

  const std::wstring& preedit = context->preedit.str;
  if (FAILED(pRangeComposition->SetText(ec, 0, preedit.c_str(),
                                        static_cast<LONG>(preedit.length()))))
    return E_FAIL;

  int selCursor = -1;
  for (const auto& attr : context->preedit.attributes) {
    if (attr.type == weasel::HIGHLIGHTED) {
      selCursor = attr.range.cursor;
      break;
    }
  }

  _SetCompositionDisplayAttributes(ec, pContext, pRangeComposition);

  LONG cch = 0;
  TF_SELECTION tfSelection;
  if (selCursor < 0) {
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  } else {
    pRangeComposition->Collapse(ec, TF_ANCHOR_START);
    pRangeComposition->ShiftStart(ec, selCursor, &cch, NULL);
  }
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  pContext->SetSelection(ec, 1, &tfSelection);
  return S_OK;
}

class CInlinePreeditEditSession : public CEditSession {
 public:
  CInlinePreeditEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            std::shared_ptr<weasel::Context> context,
                            UINT64 pendingUIGeneration)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _context(std::move(context)),
        _pendingUIGeneration(pendingUIGeneration) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return _pTextService->_ShowInlinePreeditLocked(
        ec, _pContext, _pComposition, _context, _pendingUIGeneration);
  }

 private:
  com_ptr<ITfComposition> _pComposition;
  std::shared_ptr<weasel::Context> _context;
  UINT64 _pendingUIGeneration;
};

BOOL WeaselTSF::_ShowInlinePreedit(
    com_ptr<ITfContext> pContext,
    const std::shared_ptr<weasel::Context> context) {
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  const UINT64 pendingUIGeneration = _pendingUIGeneration;
  com_ptr<CInlinePreeditEditSession> pEditSession;
  pEditSession.Attach(new CInlinePreeditEditSession(
      this, pContext, pComposition, context, pendingUIGeneration));
  if (!pEditSession)
    return FALSE;
  HRESULT hr = E_FAIL;
  pContext->RequestEditSession(_tfClientId, pEditSession,
                               TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  return TRUE;
}

/* Update Composition */
HRESULT WeaselTSF::_InsertTextLocked(TfEditCookie ec,
                                     com_ptr<ITfContext> pContext,
                                     com_ptr<ITfComposition> pComposition,
                                     const std::wstring& text) {
  if (!pContext)
    return E_INVALIDARG;
  if (pComposition != nullptr) {
    com_ptr<ITfRange> pRange;
    if (FAILED(pComposition->GetRange(&pRange)))
      return E_FAIL;
    if (FAILED(pRange->SetText(ec, 0, text.c_str(),
                               static_cast<LONG>(text.length()))))
      return E_FAIL;

    pRange->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION tfSelection;
    tfSelection.range = pRange;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;
    pContext->SetSelection(ec, 1, &tfSelection);
    return S_OK;
  }

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  if (FAILED(pContext->QueryInterface(IID_ITfInsertAtSelection,
                                      (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  com_ptr<ITfRange> pRange;
  return pInsertAtSelection->InsertTextAtSelection(
      ec, 0, text.c_str(), static_cast<LONG>(text.length()), &pRange);
}

class CInsertTextEditSession : public CEditSession {
 public:
  CInsertTextEditSession(com_ptr<WeaselTSF> pTextService,
                         com_ptr<ITfContext> pContext,
                         com_ptr<ITfComposition> pComposition,
                         const std::wstring& text)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _text(text) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return _pTextService->_InsertTextLocked(ec, _pContext, _pComposition,
                                            _text);
  }

 private:
  com_ptr<ITfComposition> _pComposition;
  std::wstring _text;
};

BOOL WeaselTSF::_InsertText(com_ptr<ITfContext> pContext,
                            const std::wstring& text) {
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  CInsertTextEditSession* pEditSession =
      new CInsertTextEditSession(this, pContext, pComposition, text);
  if (!pEditSession)
    return FALSE;
  HRESULT hr = E_FAIL;
  pContext->RequestEditSession(_tfClientId, pEditSession,
                               TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  pEditSession->Release();
  return TRUE;
}

/* Consume Server Response */
bool WeaselTSF::_ConsumeResponse(LPWSTR buffer, DWORD length) {
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  _parser.p_commit = &commit;
  _parser.p_context = context.get();
  _parser.p_status = &_status;
  _parser.p_config = &config;
  _parser.p_style = &_cand->style();
  bool ok = _parser(buffer, length);
  if (!ok)
    return false;
  _pendingCommit.append(commit);
  _context = std::move(context);
  _config = config;
  _UpdateLanguageBar(_status);
  return true;
}

void WeaselTSF::_ConsumeResponseIfFresh() {
  const UINT64 serial = m_client.ResponseSerial();
  if (serial == _parsedSerial)
    return;

  bool parsed = false;
  m_client.GetResponseData([this, &parsed](LPWSTR buffer, DWORD length) {
    parsed = _ConsumeResponse(buffer, length);
    return parsed;
  });
  if (parsed)
    _parsedSerial = serial;
}

class CApplyResponseEditSession : public CEditSession {
 public:
  CApplyResponseEditSession(
      com_ptr<WeaselTSF> pTextService,
      const std::shared_ptr<WeaselTSF::ResponseApplyRequest>& request,
      UINT64 flightToken,
      ResponseApplyQueue* queue)
      : CEditSession(pTextService, request ? request->context : nullptr),
        _request(request),
        _flightToken(flightToken),
        _queue(queue) {}

  ~CApplyResponseEditSession() override {
    const bool ownsFlight =
        _queue && _queue->InFlight() && _queue->FlightToken() == _flightToken;
    if (ownsFlight && _resetOnDispose && _ShouldResetEngine()) {
      _pTextService->_AbortComposition();
    } else if (_request && weasel::tsf::ShouldRetireCandidateOnApplyDispose(
                               _executed, _resetOnDispose, ownsFlight)) {
      // An accepted async edit session can be destroyed without ever
      // receiving DoEditSession. Retire only its matching UIElement so the
      // host cannot be left with a permanently locked Finalize.
      _pTextService->_RetireUnappliedCandidateFinalization(
          _request->context, _request->snapshot.identity.serial);
    }
    _CompleteFlight();
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    if (!_queue || !_queue->InFlight() || _queue->FlightToken() != _flightToken)
      return S_OK;

    _executed = true;
    HRESULT hr = E_FAIL;
    if (_request && _request->context) {
      hr = _pTextService->_ApplyResponseSnapshot(ec, _request->context,
                                                 _request->snapshot);
    }

    if (FAILED(hr) && _request) {
      const auto failure = ClassifyApplyFailure(hr);
      if (weasel::tsf::CanRetryApplyFailure(
              _request->snapshot.progress, failure,
              _request->snapshot.retries.apply_retries)) {
        ++_request->snapshot.retries.apply_retries;
        if (_queue->RestoreFront(_request, _flightToken)) {
          _queue->RequestRetry();
          return hr;
        }
      }
      _resetOnDispose = true;
      return hr;
    }

    _CompleteFlight();
    return hr;
  }

  bool Executed() const { return _executed; }

 private:
  bool _ShouldResetEngine() const {
    if (!_request || !_queue)
      return false;
    return weasel::tsf::ShouldResetEngineAfterResponseFailure(
        _request->snapshot.identity, _queue->CurrentEpoch(),
        !_request->snapshot.commit.empty());
  }

  void _CompleteFlight() {
    if (_queue && _queue->Complete(_flightToken))
      _pTextService->_ScheduleNextResponseApply(false);
  }

  std::shared_ptr<WeaselTSF::ResponseApplyRequest> _request;
  UINT64 _flightToken;
  ResponseApplyQueue* _queue;
  bool _executed = false;
  bool _resetOnDispose = false;
};

class CContextHandoffEditSession : public CEditSession {
 public:
  CContextHandoffEditSession(
      com_ptr<WeaselTSF> pTextService,
      com_ptr<ITfContext> pContext,
      com_ptr<ITfComposition> pComposition,
      const std::shared_ptr<WeaselTSF::ResponseApplyRequest>& request,
      UINT64 flightToken,
      ResponseApplyQueue* queue)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _request(request),
        _flightToken(flightToken),
        _queue(queue) {}

  ~CContextHandoffEditSession() override {
    const bool ownsFlight =
        _queue && _queue->InFlight() && _queue->FlightToken() == _flightToken;
    if (weasel::tsf::ShouldWaitForAbandonedHandoff(
            _executed, ownsFlight,
            _pTextService->_IsCurrentComposition(_pComposition)))
      _pTextService->_WaitForHandoffEdit(_pContext, _pComposition);
    _CompleteFlight();
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    if (!_queue || !_queue->InFlight() || _queue->FlightToken() != _flightToken)
      return S_OK;
    _executed = true;
    const HRESULT hr = _pTextService->_EndCompositionLocked(
        ec, _pContext, _pComposition, TRUE, TRUE);
    if (FAILED(hr)) {
      if (_request && _request->snapshot.retries.CanRetryHandoff()) {
        ++_request->snapshot.retries.handoff_retries;
        if (_queue->Complete(_flightToken))
          _queue->RequestRetry();
        return hr;
      }
      if (_queue->InFlight() && _queue->FlightToken() == _flightToken)
        _pTextService->_RetainComposition(_pContext, _pComposition);
      _CompleteFlight();
      return hr;
    }
    _CompleteFlight();
    return hr;
  }

 private:
  void _CompleteFlight() {
    if (_queue && _queue->Complete(_flightToken))
      _pTextService->_ScheduleNextResponseApply(false);
  }

  com_ptr<ITfComposition> _pComposition;
  std::shared_ptr<WeaselTSF::ResponseApplyRequest> _request;
  UINT64 _flightToken;
  ResponseApplyQueue* _queue;
  bool _executed = false;
};

// One outstanding old-context edit barrier. Later input may also release it
// using the queued response's existing handoff retry budget.
void WeaselTSF::_WaitForHandoffEdit(com_ptr<ITfContext> context,
                                    com_ptr<ITfComposition> composition) {
  if (!context || !composition || !_IsCurrentComposition(composition) ||
      _handoffWaitPauseToken != 0 || _responseApplyQueue.IsPaused())
    return;
  _handoffWaitContext = context;
  _handoffWaitPauseToken = _responseApplyQueue.Pause();
}

void WeaselTSF::_ResumeHandoffWait() {
  if (_handoffWaitPauseToken == 0)
    return;
  const UINT64 token = _handoffWaitPauseToken;
  _handoffWaitPauseToken = 0;
  _handoffWaitContext.Release();
  if (_responseApplyQueue.Resume(token))
    _ScheduleNextResponseApply(false);
}

void WeaselTSF::_ResumeHandoffOnInput() {
  if (_handoffWaitPauseToken == 0)
    return;

  const auto request = _responseApplyQueue.PeekFront();
  if (!request)
    return;
  const bool ownsPause =
      _responseApplyQueue.IsPaused() &&
      _responseApplyQueue.PauseToken() == _handoffWaitPauseToken;
  const auto action = weasel::tsf::ClassifyHandoffInput(
      ownsPause, _responseApplyQueue.InFlight(),
      request->snapshot.retries.handoff_retries);
  if (action == weasel::tsf::HandoffInputAction::KeepWaiting)
    return;

  if (action == weasel::tsf::HandoffInputAction::Retry) {
    ++request->snapshot.retries.handoff_retries;
    LOG(INFO) << "Resuming abandoned TSF handoff on input, retry="
              << request->snapshot.retries.handoff_retries;
    _CancelHandoffWait();
    return;
  }

  // Transfer ownership before releasing the barrier. The old host composition
  // must be ended under its own write lock, or restored if input returns there.
  const UINT64 pauseToken = _handoffWaitPauseToken;
  const auto oldContext = _handoffWaitContext;
  const auto oldComposition = _pComposition;
  LOG(WARNING) << "Retaining old TSF composition for safe handoff cleanup";
  _RetainComposition(oldContext, oldComposition);
  if (_handoffWaitPauseToken == pauseToken)
    _CancelHandoffWait();
}

void WeaselTSF::_CancelHandoffWait() {
  if (_handoffWaitPauseToken == 0)
    return;
  const UINT64 token = _handoffWaitPauseToken;
  _handoffWaitPauseToken = 0;
  _handoffWaitContext.Release();
  // Teardown decides whether pending responses survive. Never schedule here.
  _responseApplyQueue.Resume(token);
}

void WeaselTSF::_EnqueueResponseApply(
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot,
    UINT64 responseSerial) {
  auto request = std::make_shared<ResponseApplyRequest>();
  request->context = pContext;
  request->snapshot = std::move(snapshot);
  request->responseSerial = responseSerial;
  _responseApplyQueue.Push(std::move(request));
  // New input is a host callback boundary, outside the abandoned session's
  // Release stack. Do not depend forever on another edit in its old context.
  _ResumeHandoffOnInput();
  _ScheduleRetainedCompositionCleanup();
  _ScheduleNextResponseApply(true);
}

void WeaselTSF::_ScheduleNextResponseApply(bool allowSynchronous) {
  if (_tfClientId == TF_CLIENTID_NULL)
    return;

  while (_responseApplyQueue.CanSchedule()) {
    auto candidate = _responseApplyQueue.PeekFront();
    if (!candidate || !candidate->context)
      return;
    const bool stale = candidate->snapshot.identity.serial !=
                           weasel::tsf::kInvalidResponseSerial &&
                       candidate->snapshot.identity.engine_epoch !=
                           _responseApplyQueue.CurrentEpoch();
    if (!stale || !candidate->snapshot.commit.empty())
      break;
    auto discarded = _responseApplyQueue.TakeNext();
    const UINT64 discardToken = _responseApplyQueue.FlightToken();
    (void)discarded;
    _responseApplyQueue.Complete(discardToken);
  }

  if (!_responseApplyQueue.CanSchedule())
    return;

  auto next = _responseApplyQueue.PeekFront();
  if (!next || !next->context)
    return;

  const DWORD scheduleFlag =
      allowSynchronous ? TF_ES_ASYNCDONTCARE : TF_ES_ASYNC;

  if (_IsComposing() && !_IsComposingInContext(next->context)) {
    com_ptr<ITfContext> previousContext = _pCompositionContext;
    com_ptr<ITfComposition> previousComposition = _pComposition;
    if (!previousContext || !previousComposition ||
        !_responseApplyQueue.BeginBarrier())
      return;

    const UINT64 flightToken = _responseApplyQueue.FlightToken();
    com_ptr<CContextHandoffEditSession> session;
    session.Attach(new CContextHandoffEditSession(
        this, previousContext, previousComposition, next, flightToken,
        &_responseApplyQueue));
    if (!session) {
      _responseApplyQueue.Complete(flightToken);
      return;
    }

    HRESULT sessionHr = E_FAIL;
    const HRESULT requestHr = previousContext->RequestEditSession(
        _tfClientId, session, scheduleFlag | TF_ES_READWRITE, &sessionHr);
    if (FAILED(requestHr)) {
      if (requestHr == TF_E_DISCONNECTED) {
        if (_responseApplyQueue.Complete(flightToken) &&
            _IsCurrentComposition(previousComposition)) {
          _EndUI();
          _FinalizeComposition();
          _ScheduleNextResponseApply(false);
        }
      } else if (requestHr == TF_E_LOCKED &&
                 next->snapshot.retries.CanRetryHandoff()) {
        ++next->snapshot.retries.handoff_retries;
        if (_responseApplyQueue.Complete(flightToken))
          _responseApplyQueue.RequestRetry();
      } else {
        if (_responseApplyQueue.InFlight() &&
            _responseApplyQueue.FlightToken() == flightToken)
          _RetainComposition(previousContext, previousComposition);
        if (_responseApplyQueue.Complete(flightToken))
          _ScheduleNextResponseApply(false);
      }
      return;
    }

    if (sessionHr == TS_E_READONLY && _responseApplyQueue.InFlight() &&
        _responseApplyQueue.FlightToken() == flightToken) {
      _RetainComposition(previousContext, previousComposition);
      if (_responseApplyQueue.Complete(flightToken))
        _ScheduleNextResponseApply(false);
      return;
    }

    if (sessionHr != TF_S_ASYNC && _responseApplyQueue.InFlight() &&
        _responseApplyQueue.FlightToken() == flightToken) {
      _RetainComposition(previousContext, previousComposition);
      if (_responseApplyQueue.Complete(flightToken))
        _ScheduleNextResponseApply(false);
    }
    return;
  }

  auto request = _responseApplyQueue.TakeNext();
  if (!request)
    return;
  const UINT64 flightToken = _responseApplyQueue.FlightToken();
  com_ptr<ITfContext> failedContext = request->context;
  const auto failureIdentity = request->snapshot.identity;
  const bool failureHasCommit = !request->snapshot.commit.empty();
  auto shouldResetRequestEngine = [&]() {
    return weasel::tsf::ShouldResetEngineAfterResponseFailure(
        failureIdentity, _responseApplyQueue.CurrentEpoch(), failureHasCommit);
  };
  auto settleTerminalFailure = [&]() {
    if (!_responseApplyQueue.Complete(flightToken))
      return;
    if (shouldResetRequestEngine()) {
      _AbortComposition();
    } else {
      _RetireUnappliedCandidateFinalization(failedContext,
                                            failureIdentity.serial);
      _ScheduleNextResponseApply(false);
    }
  };

  com_ptr<CApplyResponseEditSession> session;
  session.Attach(new CApplyResponseEditSession(this, request, flightToken,
                                               &_responseApplyQueue));
  if (!session) {
    _responseApplyQueue.RestoreFront(std::move(request), flightToken);
    return;
  }

  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = request->context->RequestEditSession(
      _tfClientId, session, scheduleFlag | TF_ES_READWRITE, &sessionHr);
  if (FAILED(requestHr)) {
    const auto action = weasel::tsf::ClassifyScheduleFailure(
        requestHr == TF_E_DISCONNECTED, false, requestHr == TF_E_LOCKED);
    if (action == weasel::tsf::ScheduleFailureAction::RetryLater &&
        request->snapshot.retries.CanRetryApply()) {
      ++request->snapshot.retries.apply_retries;
      if (_responseApplyQueue.RestoreFront(std::move(request), flightToken)) {
        _responseApplyQueue.RequestRetry();
        return;
      }
    }

    settleTerminalFailure();
    return;
  }

  if (!session->Executed() && sessionHr == TS_E_READONLY &&
      _responseApplyQueue.InFlight() &&
      _responseApplyQueue.FlightToken() == flightToken) {
    settleTerminalFailure();
    return;
  }

  if (!session->Executed() && sessionHr != TF_S_ASYNC &&
      _responseApplyQueue.InFlight() &&
      _responseApplyQueue.FlightToken() == flightToken) {
    settleTerminalFailure();
  }
}

void WeaselTSF::_CompleteResponseApply() {
  const UINT64 flightToken = _responseApplyQueue.FlightToken();
  if (!_responseApplyQueue.Complete(flightToken))
    return;
  _ScheduleNextResponseApply(false);
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  _UpdateComposition(pContext, _parsedSerial);
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext,
                                   UINT64 previousSerial) {
  _ConsumeResponseIfFresh();
  _pEditSessionContext = pContext;

  if (_parsedSerial == previousSerial)
    return;

  const weasel::tsf::ResponseIdentity identity{
      _parsedSerial, _responseApplyQueue.CurrentEpoch()};
  if (!weasel::tsf::ShouldEnqueueResponse(
          identity, _responseApplyQueue.LastEnqueuedSerial(),
          _responseApplyQueue.CurrentEpoch()) ||
      !_responseApplyQueue.MarkSerialEnqueued(_parsedSerial)) {
    return;
  }

  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      _pendingCommit, _context, _status, _config, identity);
  _pendingCommit.clear();
  _EnqueueResponseApply(pContext, std::move(snapshot), _parsedSerial);
}

/* Composition State */
STDMETHODIMP WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                                ITfComposition* pComposition) {
  if (_ForgetRetainedComposition(pComposition))
    return S_OK;
  if (_handoffWaitPauseToken != 0 && _IsCurrentComposition(pComposition)) {
    _FinalizeComposition();
    _EndUI();
    _ResumeHandoffWait();
    return S_OK;
  }
  if (!_IsCurrentComposition(pComposition))
    return S_OK;

  if (_status.composing) {
    _FinalizeComposition();
    _ScheduleNextResponseApply(true);
    return S_OK;
  }

  _AbortComposition();
  return S_OK;
}

void WeaselTSF::_AbortComposition(bool clear,
                                  DWORD clear_timeout_ms,
                                  DWORD editSessionFlags) {
  _CancelHandoffWait();
  _InvalidatePendingUI();
  _responseApplyQueue.AdvanceEpoch();
  m_client.ClearComposition(clear_timeout_ms);

  const bool hadComposition = _IsComposing();
  if (hadComposition) {
    _responseApplyQueue.Pause();
    com_ptr<ITfContext> compositionContext =
        _pCompositionContext ? _pCompositionContext : _pEditSessionContext;
    _EndComposition(compositionContext, clear, TRUE, editSessionFlags);
  } else {
    // The host may terminate the TSF composition before focus-loss cleanup.
    // End the candidate UI even when the composition pointer is already gone.
    _cand->EndUI();
  }
  _committed = TRUE;
  _cand->Destroy();
  _pCandidateUIContext = nullptr;
  if (!hadComposition)
    _ScheduleNextResponseApply(false);
}

void WeaselTSF::_FinalizeComposition() {
  _pComposition = nullptr;
  _pCompositionContext = nullptr;
}

void WeaselTSF::_SetComposition(com_ptr<ITfComposition> pComposition,
                                com_ptr<ITfContext> pContext) {
  _pComposition = pComposition;
  _pCompositionContext = pContext;
}

BOOL WeaselTSF::_IsComposing() {
  return _pComposition != NULL;
}

BOOL WeaselTSF::_IsComposingInContext(ITfContext* pContext) {
  return _pComposition != nullptr && _pCompositionContext != nullptr &&
         _pCompositionContext == pContext;
}

BOOL WeaselTSF::_IsCurrentComposition(ITfComposition* pComposition) {
  return _pComposition != nullptr && _pComposition == pComposition;
}
