#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"

/* Start Composition */
class CStartCompositionEditSession : public CEditSession {
 public:
  CStartCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                               com_ptr<ITfContext> pContext,
                               BOOL fCUASWorkaroundEnabled,
                               UINT64 pendingUIGeneration)
      : CEditSession(pTextService, pContext),
        _pendingUIGeneration(pendingUIGeneration) {
    _fCUASWorkaroundEnabled = fCUASWorkaroundEnabled;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  BOOL _fCUASWorkaroundEnabled;
  UINT64 _pendingUIGeneration;
};

STDMETHODIMP CStartCompositionEditSession::DoEditSession(TfEditCookie ec) {
  // A newer server response can arrive while this asynchronous edit session
  // waits for the document lock. Never create a composition for an obsolete
  // pending candidate snapshot.
  if (_pendingUIGeneration != 0 &&
      !_pTextService->_IsPendingUIGenerationCurrent(_pendingUIGeneration))
    return S_OK;

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeSelection;
  com_ptr<ITfRange> pRangeComposition;
  if (_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                (LPVOID*)&pInsertAtSelection) != S_OK)
    return E_FAIL;
  if (pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0,
                                                &pRangeSelection) != S_OK)
    return E_FAIL;

  // Typing replaces the selection: delete it and re-query the collapsed
  // insertion point. A rejected edit keeps the previous behavior.
  pRangeSelection->SetText(ec, 0, L"", 0);
  if (pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0,
                                                &pRangeComposition) != S_OK)
    return E_FAIL;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  if (_pContext->QueryInterface(IID_ITfContextComposition,
                                (LPVOID*)&pContextComposition) != S_OK)
    return E_FAIL;
  if ((pContextComposition->StartComposition(
           ec, pRangeComposition, _pTextService, &pComposition) == S_OK) &&
      (pComposition != NULL)) {
    _pTextService->_SetComposition(pComposition, _pContext);

    /* set selection */
    TF_SELECTION tfSelection;
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
    tfSelection.range = pRangeComposition;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;
    _pContext->SetSelection(ec, 1, &tfSelection);

    // The old composition's range is still visible while its asynchronous
    // end session is pending. Position only after the new composition has
    // actually been created, not from the response handler's stale range.
    _pTextService->_UpdateCompositionWindow(_pContext, _pendingUIGeneration);

    return S_OK;
  }

  // composition could not be created
  return E_FAIL;
}

void WeaselTSF::_StartComposition(com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled,
                                  UINT64 pendingUIGeneration) {
  com_ptr<CStartCompositionEditSession> pStartCompositionEditSession;
  pStartCompositionEditSession.Attach(new CStartCompositionEditSession(
      this, pContext, fCUASWorkaroundEnabled, pendingUIGeneration));
  _cand->StartUI();
  if (pStartCompositionEditSession != nullptr) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
}

/* End Composition */
class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             BOOL clear = TRUE)
      : CEditSession(pTextService, pContext), _clear(clear) {
    _pComposition = pComposition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
};

STDMETHODIMP CEndCompositionEditSession::DoEditSession(TfEditCookie ec) {
  /* Clear the dummy text we set before, if any. */
  if (_pComposition == nullptr)
    return S_OK;
  // Avoid null pointer dereference
  if (!_pTextService || !_pContext)
    return S_OK;

  _pTextService->_ClearCompositionDisplayAttributes(ec, _pContext,
                                                    _pComposition);

  com_ptr<ITfRange> pCompositionRange;
  if (_clear && _pComposition->GetRange(&pCompositionRange) == S_OK)
    pCompositionRange->SetText(ec, 0, L"", 0);

  // Drop ownership before EndComposition(). Some applications notify
  // OnCompositionTerminated synchronously while the old composition ends.
  // Keeping it as the current composition makes that normal notification
  // look like an external abort and can clear a new Rime composition during
  // auto-commit.
  if (_pTextService && _pTextService->_IsCurrentComposition(_pComposition))
    _pTextService->_FinalizeComposition();
  _pComposition->EndComposition(ec);
  return S_OK;
}

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext,
                                BOOL clear,
                                BOOL endUI) {
  CEndCompositionEditSession* pEditSession;
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;

  if (endUI)
    _cand->EndUI();
  if ((pEditSession = new CEndCompositionEditSession(
           this, pContext, pComposition, clear)) != NULL) {
    HRESULT sessionHr = E_FAIL;
    const HRESULT requestHr = pContext->RequestEditSession(
        _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &sessionHr);
    // Once TSF accepts the end request, this composition is committed to
    // teardown even if the edit session runs later. Release local ownership
    // now so a response for another ITfContext cannot overwrite it. If the
    // request itself fails, retain ownership and block the cross-context handoff.
    if (SUCCEEDED(requestHr) && _IsCurrentComposition(pComposition))
      _FinalizeComposition();
    pEditSession->Release();
  }
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
        _pendingUIGeneration(pendingUIGeneration) {
    _pContextView = pContextView;
    _pComposition = pComposition;
    _enhancedPosition = enhancedPosition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  bool _enhancedPosition;
  UINT64 _pendingUIGeneration;
};

STDMETHODIMP CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) {
  // A delayed extent from response A must not reposition or publish response B.
  if (_pendingUIGeneration != 0 &&
      !_pTextService->_IsPendingUIGenerationCurrent(_pendingUIGeneration))
    return S_OK;

  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  ITfRange* pRange;
  RECT rc;
  BOOL fClipped;
  TF_SELECTION selection;
  ULONG nSelection;

  if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                       (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                     &nSelection)))
    return E_FAIL;

  if (_pComposition != nullptr && _pComposition->GetRange(&pRange) == S_OK) {
    pRange->Collapse(ec, TF_ANCHOR_START);
    // GetSelection AddRef'd this range and we did not adopt it above.
    selection.range->Release();
  } else {
    // composition end
    // note: selection.range is always an empty range
    // Adopt the single reference GetSelection gave us.
    pRange = selection.range;
  }

  if ((_pContextView->GetTextExt(ec, pRange, &rc, &fClipped)) == S_OK &&
      (rc.left != 0 || rc.top != 0)) {
    // get the foreground window pos and check if rc from GetTextExt is out of
    // window
    if (_enhancedPosition) {
      HWND hwnd;
      RECT rcForegroundWindow;
      hwnd = GetForegroundWindow();
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
  // pRange holds exactly one reference on every path: GetRange's, or the
  // selection reference adopted above.
  pRange->Release();
  return S_OK;
}

/* Composition Window Handling */
BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext,
                                         UINT64 pendingUIGeneration) {
  com_ptr<ITfContextView> pContextView;
  if (pContext->GetActiveView(&pContextView) != S_OK)
    return FALSE;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  pEditSession.Attach(new CGetTextExtentEditSession(
      this, pContext, pContextView, pComposition,
      _cand->style().enhanced_position, pendingUIGeneration));
  if (pEditSession == NULL) {
    return FALSE;
  }
  HRESULT hr;
  pContext->RequestEditSession(_tfClientId, pEditSession,
                               TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
  return SUCCEEDED(hr);
}

void WeaselTSF::_SetCompositionPosition(const RECT& rc,
                                        UINT64 pendingUIGeneration) {
  // Re-check at the publication boundary. Even if the edit session was current
  // when it started, a newer response must win if it arrived before this call.
  if (pendingUIGeneration != 0 &&
      !_IsPendingUIGenerationCurrent(pendingUIGeneration))
    return;

  /* Test if rect is valid.
   * If it is invalid during CUAS test, we need to apply CUAS workaround */
  if (!_fCUASWorkaroundTested) {
    _fCUASWorkaroundTested = TRUE;
    if (rc.top == rc.bottom) {
      _fCUASWorkaroundEnabled = TRUE;
      return;
    }
  }
  RECT _rc;
  _rc.left = _rc.right = rc.left;
  _rc.top = _rc.bottom = rc.bottom;
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
  _pendingUIContext.reset();
  _hasPendingUI = false;
}

bool WeaselTSF::_IsPendingUIGenerationCurrent(UINT64 generation) const {
  return generation != 0 && _hasPendingUI &&
         generation == _pendingUIGeneration && _pendingUIContext != nullptr;
}

bool WeaselTSF::_TakePendingUI(UINT64 generation,
                               std::shared_ptr<weasel::Context>& context,
                               weasel::Status& status) {
  if (!_IsPendingUIGenerationCurrent(generation))
    return false;
  context = std::move(_pendingUIContext);
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
class CInlinePreeditEditSession : public CEditSession {
 public:
  CInlinePreeditEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            const std::shared_ptr<weasel::Context> context)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _context(context) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  const std::shared_ptr<weasel::Context> _context;
};

STDMETHODIMP CInlinePreeditEditSession::DoEditSession(TfEditCookie ec) {
  std::wstring preedit = _context->preedit.str;

  com_ptr<ITfRange> pRangeComposition;
  if (_pComposition == nullptr)
    return E_FAIL;
  if ((_pComposition->GetRange(&pRangeComposition)) != S_OK)
    return E_FAIL;

  if ((pRangeComposition->SetText(ec, 0, preedit.c_str(),
                                  static_cast<LONG>(preedit.length()))) != S_OK)
    return E_FAIL;

  /* TODO: Check the availability and correctness of these values */
  int sel_cursor = -1;
  for (size_t i = 0; i < _context->preedit.attributes.size(); i++) {
    if (_context->preedit.attributes.at(i).type == weasel::HIGHLIGHTED) {
      sel_cursor = _context->preedit.attributes.at(i).range.cursor;
      break;
    }
  }

  _pTextService->_SetCompositionDisplayAttributes(ec, _pContext,
                                                  pRangeComposition);

  /* Set caret */
  LONG cch;
  TF_SELECTION tfSelection;
  if (sel_cursor < 0) {
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  } else {
    pRangeComposition->Collapse(ec, TF_ANCHOR_START);
    pRangeComposition->ShiftStart(ec, sel_cursor, &cch, NULL);
  }
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  _pContext->SetSelection(ec, 1, &tfSelection);

  return S_OK;
}

BOOL WeaselTSF::_ShowInlinePreedit(
    com_ptr<ITfContext> pContext,
    const std::shared_ptr<weasel::Context> context) {
  com_ptr<CInlinePreeditEditSession> pEditSession;
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;
  pEditSession.Attach(
      new CInlinePreeditEditSession(this, pContext, pComposition, context));
  if (pEditSession != NULL) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
  return TRUE;
}

/* Update Composition */
class CInsertTextEditSession : public CEditSession {
 public:
  CInsertTextEditSession(com_ptr<WeaselTSF> pTextService,
                         com_ptr<ITfContext> pContext,
                         com_ptr<ITfComposition> pComposition,
                         const std::wstring& text)
      : CEditSession(pTextService, pContext),
        _text(text),
        _pComposition(pComposition) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  std::wstring _text;
  com_ptr<ITfComposition> _pComposition;
};

STDMETHODIMP CInsertTextEditSession::DoEditSession(TfEditCookie ec) {
  if (_pComposition != nullptr) {
    com_ptr<ITfRange> pRange;
    if (FAILED(_pComposition->GetRange(&pRange)))
      return E_FAIL;

    if (FAILED(pRange->SetText(ec, 0, _text.c_str(),
                               static_cast<LONG>(_text.length()))))
      return E_FAIL;

    /* update the selection to an insertion point just past the inserted
     * text. */
    pRange->Collapse(ec, TF_ANCHOR_END);

    TF_SELECTION tfSelection;
    tfSelection.range = pRange;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;

    _pContext->SetSelection(ec, 1, &tfSelection);
    return S_OK;
  }

  // No composition in this response's context. Insert the commit directly at
  // that context's selection instead of creating an asynchronous throwaway
  // composition whose matching EndComposition might never capture it.
  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                       (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  com_ptr<ITfRange> pRange;
  if (FAILED(pInsertAtSelection->InsertTextAtSelection(
          ec, TF_IAS_NO_DEFAULT_COMPOSITION, _text.c_str(),
          static_cast<LONG>(_text.length()), &pRange)))
    return E_FAIL;
  return S_OK;
}

BOOL WeaselTSF::_InsertText(com_ptr<ITfContext> pContext,
                            const std::wstring& text) {
  CInsertTextEditSession* pEditSession;
  HRESULT hr;
  com_ptr<ITfComposition> pComposition =
      _IsComposingInContext(pContext) ? _pComposition : nullptr;

  if ((pEditSession = new CInsertTextEditSession(this, pContext, pComposition,
                                                 text)) != NULL) {
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    pEditSession->Release();
  }

  return TRUE;
}

/* Consume Server Response */
bool WeaselTSF::_ConsumeResponse(LPWSTR buffer, DWORD length) {
  std::wstring commit;
  weasel::Config config;
  // Context 仍按键新建：排队中的编辑会话（如 CInlinePreeditEditSession）
  // 持有旧快照的 shared_ptr 副本，复用同一分配会让异步会话读到后续键的
  // 数据（01885fc 的快照隔离语义，承重，保留）
  auto context = std::make_shared<weasel::Context>();
  // 复用成员 parser 免去每键重建 deserializer 注册表；各 deserializer
  // 均在 Store 时经 m_pTarget->p_* 读目标，解析前重指即可
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
  // 语言栏/输入指示随解析即时更新，不依赖可能被排队延迟的编辑会话
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

class CApplyResponseEditSession : public CEditSession {
 public:
  CApplyResponseEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            weasel::tsf::ResponseApplySnapshot snapshot)
      : CEditSession(pTextService, pContext),
        _snapshot(std::move(snapshot)) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    return _pTextService->_ApplyResponseSnapshot(_pContext,
                                                 std::move(_snapshot));
  }

 private:
  weasel::tsf::ResponseApplySnapshot _snapshot;
};

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  // Parse while the IPC response buffer still belongs to this key/callback,
  // then freeze the result into the edit-session object. A later key or a
  // focus/context switch may replace the TSF's current response members before
  // this session receives a document lock; it must never observe those values.
  _ConsumeResponseIfFresh();

  _pEditSessionContext = pContext;
  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      _pendingCommit, _context, _status, _config);

  com_ptr<CApplyResponseEditSession> pApplySession;
  pApplySession.Attach(
      new CApplyResponseEditSession(this, pContext, std::move(snapshot)));
  if (!pApplySession)
    return;

  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pApplySession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
      &sessionHr);
  if (SUCCEEDED(requestHr)) {
    // This commit now belongs exclusively to the queued immutable snapshot.
    // If RequestEditSession itself failed, leave it pending for a later retry.
    _pendingCommit.clear();
  }
}

/* Composition State */
STDMETHODIMP WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                                ITfComposition* pComposition) {
  // NOTE:
  // This will be called when an edit session ended up with an empty composition
  // string, Even if it is closed normally. Silly M$.

  // EndComposition() may generate this callback for the composition we just
  // closed. Only an active, matching composition is an external termination.
  if (!_IsCurrentComposition(pComposition))
    return S_OK;

  // A host may terminate the empty TSF composition used for a non-inline
  // preedit. Keep Rime's composing state; the next key will create a fresh
  // TSF composition. Only an inactive Rime session should be aborted here.
  if (_status.composing) {
    _FinalizeComposition();
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
