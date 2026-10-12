#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"
#include "AsyncUiPolicy.h"

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
  com_ptr<ITfRange> pRangeSelection;
  com_ptr<ITfRange> pRangeComposition;
  if (FAILED(pContext->QueryInterface(IID_ITfInsertAtSelection,
                                      (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  if (FAILED(pInsertAtSelection->InsertTextAtSelection(
          ec, TF_IAS_QUERYONLY, NULL, 0, &pRangeSelection)))
    return E_FAIL;

  // Typing replaces the selection: delete it and re-query the collapsed
  // insertion point. Keep the historical best-effort deletion behavior.
  pRangeSelection->SetText(ec, 0, L"", 0);
  if (FAILED(pInsertAtSelection->InsertTextAtSelection(
          ec, TF_IAS_QUERYONLY, NULL, 0, &pRangeComposition)))
    return E_FAIL;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  if (FAILED(pContext->QueryInterface(IID_ITfContextComposition,
                                      (LPVOID*)&pContextComposition)))
    return E_FAIL;
  HRESULT hr = pContextComposition->StartComposition(
      ec, pRangeComposition, this, &pComposition);
  if (FAILED(hr) || pComposition == nullptr)
    return FAILED(hr) ? hr : E_FAIL;

  // StartComposition can re-enter TSF code. If a later response generation was
  // already applied, discard the just-created stale composition before it can
  // own UI or document state.
  const bool generationCurrent =
      pendingUIGeneration == 0 ||
      _IsPendingUIGenerationCurrent(pendingUIGeneration);
  if (!generationCurrent) {
    pComposition->EndComposition(ec);
    return S_OK;
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
      _pTextService->_UpdateCompositionWindow(_pContext,
                                              _pendingUIGeneration);
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

  com_ptr<ITfRange> pCompositionRange;
  if (clear && SUCCEEDED(pComposition->GetRange(&pCompositionRange)))
    pCompositionRange->SetText(ec, 0, L"", 0);

  // Release local ownership before EndComposition: hosts may notify
  // OnCompositionTerminated synchronously from this call.
  if (_IsCurrentComposition(pComposition))
    _FinalizeComposition();
  return pComposition->EndComposition(ec);
}

class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             BOOL clear)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _clear(clear) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return _pTextService->_EndCompositionLocked(
        ec, _pContext, _pComposition, _clear, FALSE);
  }

 private:
  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
};

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext,
                                BOOL clear,
                                BOOL endUI) {
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;

  if (endUI) {
    _EndUI();
  }

  CEndCompositionEditSession* pEditSession =
      new CEndCompositionEditSession(this, pContext, pComposition, clear);
  if (!pEditSession)
    return;

  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
      &sessionHr);
  // Once TSF accepts the end request, prevent a later context from overwriting
  // this composition while its physical EndComposition is still queued.
  if (SUCCEEDED(requestHr) && _IsCurrentComposition(pComposition))
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

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  ITfRange* pRange = nullptr;
  RECT rc;
  BOOL fClipped;
  TF_SELECTION selection;
  ULONG nSelection = 0;

  if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                       (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                     &nSelection)) ||
      nSelection != 1 || selection.range == nullptr)
    return E_FAIL;

  if (_pComposition != nullptr && SUCCEEDED(_pComposition->GetRange(&pRange))) {
    pRange->Collapse(ec, TF_ANCHOR_START);
    selection.range->Release();
  } else {
    pRange = selection.range;
  }

  if (SUCCEEDED(_pContextView->GetTextExt(ec, pRange, &rc, &fClipped)) &&
      (rc.left != 0 || rc.top != 0)) {
    if (_enhancedPosition) {
      HWND hwnd = GetForegroundWindow();
      RECT rcForegroundWindow;
      ::GetWindowRect(hwnd, &rcForegroundWindow);

      if (rc.left < rcForegroundWindow.left ||
          rc.left > rcForegroundWindow.right ||
          rc.top < rcForegroundWindow.top ||
          rc.top > rcForegroundWindow.bottom) {
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
  }
  pRange->Release();
  return S_OK;
}

BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext,
                                         UINT64 pendingUIGeneration) {
  if (pendingUIGeneration == 0 && _IsComposingInContext(pContext))
    pendingUIGeneration = _pendingUIGeneration;

  com_ptr<ITfContextView> pContextView;
  if (!pContext || pContext->GetActiveView(&pContextView) != S_OK)
    return FALSE;

  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  pEditSession.Attach(new CGetTextExtentEditSession(
      this, pContext, pContextView, pComposition,
      _cand->style().enhanced_position, pendingUIGeneration));
  if (!pEditSession)
    return FALSE;

  HRESULT hr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
  return SUCCEEDED(requestHr);
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
  if (FAILED(pRangeComposition->SetText(
          ec, 0, preedit.c_str(), static_cast<LONG>(preedit.length()))))
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
      ec, TF_IAS_NO_DEFAULT_COMPOSITION, text.c_str(),
      static_cast<LONG>(text.length()), &pRange);
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
  UINT64 serial = m_client.ResponseSerial();
  if (serial == _parsedSerial)
    return;
  _parsedSerial = serial;
  m_client.GetResponseData([this](LPWSTR buffer, DWORD length) {
    return _ConsumeResponse(buffer, length);
  });
}

using ResponseApplyQueue =
    weasel::tsf::SerialApplyQueue<
        std::shared_ptr<WeaselTSF::ResponseApplyRequest>>;

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

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    // Deactivate/reset can invalidate a queued TSF session before the host
    // actually runs it. Never let an obsolete flight mutate document state.
    if (!_queue || !_queue->InFlight() ||
        _queue->FlightToken() != _flightToken)
      return S_OK;

    HRESULT hr = E_FAIL;
    if (_request && _request->context) {
      hr = _pTextService->_ApplyResponseSnapshot(
          ec, _request->context, std::move(_request->snapshot));
    }
    _pTextService->_CompleteResponseApply();
    return hr;
  }

 private:
  std::shared_ptr<WeaselTSF::ResponseApplyRequest> _request;
  UINT64 _flightToken;
  ResponseApplyQueue* _queue;
};

class CContextHandoffEditSession : public CEditSession {
 public:
  CContextHandoffEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             UINT64 flightToken,
                             ResponseApplyQueue* queue)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _flightToken(flightToken),
        _queue(queue) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    if (!_queue || !_queue->InFlight() ||
        _queue->FlightToken() != _flightToken)
      return S_OK;
    const HRESULT hr = _pTextService->_EndCompositionLocked(
        ec, _pContext, _pComposition, TRUE, TRUE);
    _pTextService->_CompleteResponseApply();
    return hr;
  }

 private:
  com_ptr<ITfComposition> _pComposition;
  UINT64 _flightToken;
  ResponseApplyQueue* _queue;
};

void WeaselTSF::_EnqueueResponseApply(
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot,
    UINT64 responseSerial) {
  auto request = std::make_shared<ResponseApplyRequest>();
  request->context = pContext;
  request->snapshot = std::move(snapshot);
  request->responseSerial = responseSerial;
  _responseApplyQueue.Push(std::move(request));
  _ScheduleNextResponseApply(true);
}

void WeaselTSF::_ScheduleNextResponseApply(bool allowSynchronous) {
  if (_tfClientId == TF_CLIENTID_NULL || !_responseApplyQueue.CanSchedule())
    return;

  auto next = _responseApplyQueue.PeekFront();
  if (!next || !next->context)
    return;

  const DWORD scheduleFlag =
      allowSynchronous ? TF_ES_ASYNCDONTCARE : TF_ES_ASYNC;

  // A response for context B cannot be applied while context A still owns the
  // single tracked composition. End A first under A's own write lock; the
  // response remains at the queue head until the barrier completes.
  if (_IsComposing() && !_IsComposingInContext(next->context)) {
    com_ptr<ITfContext> previousContext = _pCompositionContext;
    com_ptr<ITfComposition> previousComposition = _pComposition;
    if (!previousContext || !previousComposition ||
        !_responseApplyQueue.BeginBarrier())
      return;

    const UINT64 flightToken = _responseApplyQueue.FlightToken();
    com_ptr<CContextHandoffEditSession> session;
    session.Attach(new CContextHandoffEditSession(
        this, previousContext, previousComposition, flightToken,
        &_responseApplyQueue));
    if (!session) {
      _responseApplyQueue.Complete(flightToken);
      return;
    }

    HRESULT sessionHr = E_FAIL;
    const HRESULT requestHr = previousContext->RequestEditSession(
        _tfClientId, session, scheduleFlag | TF_ES_READWRITE, &sessionHr);
    if (FAILED(requestHr)) {
      if (_responseApplyQueue.Complete(flightToken) &&
          requestHr == TF_E_DISCONNECTED &&
          _IsCurrentComposition(previousComposition)) {
        _EndUI();
        _FinalizeComposition();
        _ScheduleNextResponseApply(false);
      }
      return;
    }

    // ASYNCDONTCARE can return successfully without running DoEditSession
    // (for example a read-only context). Only clear this exact flight; a
    // synchronous completion may already have started the next token.
    if (sessionHr != TF_S_ASYNC && _responseApplyQueue.InFlight() &&
        _responseApplyQueue.FlightToken() == flightToken) {
      _responseApplyQueue.Complete(flightToken);
    }
    return;
  }

  auto request = _responseApplyQueue.TakeNext();
  if (!request)
    return;
  const UINT64 flightToken = _responseApplyQueue.FlightToken();

  com_ptr<CApplyResponseEditSession> session;
  session.Attach(new CApplyResponseEditSession(
      this, request, flightToken, &_responseApplyQueue));
  if (!session) {
    _responseApplyQueue.RestoreFront(std::move(request), flightToken);
    return;
  }

  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = request->context->RequestEditSession(
      _tfClientId, session, scheduleFlag | TF_ES_READWRITE, &sessionHr);
  if (FAILED(requestHr)) {
    _responseApplyQueue.RestoreFront(std::move(request), flightToken);
    return;
  }

  // If a synchronous session ran, _CompleteResponseApply already completed
  // this token (and may have started another). If it did not run, put the
  // untouched request back at the head for a later retry.
  if (sessionHr != TF_S_ASYNC && _responseApplyQueue.InFlight() &&
      _responseApplyQueue.FlightToken() == flightToken) {
    _responseApplyQueue.RestoreFront(std::move(request), flightToken);
  }
}

void WeaselTSF::_CompleteResponseApply() {
  const UINT64 flightToken = _responseApplyQueue.FlightToken();
  if (!_responseApplyQueue.Complete(flightToken))
    return;
  _ScheduleNextResponseApply(false);
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  _ConsumeResponseIfFresh();
  _pEditSessionContext = pContext;

  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      _pendingCommit, _context, _status, _config);
  _pendingCommit.clear();
  _EnqueueResponseApply(pContext, std::move(snapshot), _parsedSerial);
}

/* Composition State */
STDMETHODIMP WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                                ITfComposition* pComposition) {
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

void WeaselTSF::_AbortComposition(bool clear) {
  _InvalidatePendingUI();
  m_client.ClearComposition();
  if (_IsComposing()) {
    com_ptr<ITfContext> compositionContext =
        _pCompositionContext ? _pCompositionContext : _pEditSessionContext;
    _EndComposition(compositionContext, clear);
  }
  _committed = TRUE;
  _cand->Destroy();
  _pCandidateUIContext = nullptr;
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
