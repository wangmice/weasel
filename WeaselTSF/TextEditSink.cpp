#include "stdafx.h"
#include "WeaselTSF.h"

STDMETHODIMP WeaselTSF::OnEndEdit(ITfContext* pContext,
                                  TfEditCookie ecReadOnly,
                                  ITfEditRecord* pEditRecord) {
  BOOL fSelectionChanged;
  IEnumTfRanges* pEnumTextChanges;
  ITfRange* pRange;

  /* did the selection change? */
  if (pEditRecord->GetSelectionStatus(&fSelectionChanged) == S_OK &&
      fSelectionChanged) {
    if (_IsComposingInContext(pContext)) {
      const auto composition = _pComposition;
      const auto engineEpoch = _responseApplyQueue.CurrentEpoch();
      const auto uiGeneration = _pendingUIGeneration;
      /* if the caret moves out of composition range, stop the composition */
      TF_SELECTION tfSelection;
      ULONG cFetched;

      if (pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1,
                                 &tfSelection, &cFetched) == S_OK &&
          cFetched == 1) {
        com_ptr<ITfRange> pRangeComposition;
        if (composition->GetRange(&pRangeComposition) == S_OK) {
          LONG start = 0, end = 0;
          if (pRangeComposition->CompareStart(ecReadOnly, tfSelection.range,
                                              TF_ANCHOR_START,
                                              &start) == S_OK &&
              pRangeComposition->CompareEnd(ecReadOnly, tfSelection.range,
                                            TF_ANCHOR_END, &end) == S_OK &&
              (start > 0 || end < 0)) {
            BOOL empty = TRUE, ownWrite = TRUE;
            const auto threadMgr = _GetThreadMgr();
            com_ptr<ITfDocumentMgr> focusedDocument;
            com_ptr<ITfContext> focusedContext;
            // Only a user move out of a nonempty, focused composition resets
            // Rime. Empty bridged compositions and old-context handoffs keep
            // their existing local-only cleanup semantics.
            const bool abortEngine =
                pRangeComposition->IsEmpty(ecReadOnly, &empty) == S_OK &&
                !empty &&
                pContext->InWriteSession(_tfClientId, &ownWrite) == S_OK &&
                !ownWrite && threadMgr &&
                threadMgr->GetFocus(&focusedDocument) == S_OK &&
                focusedDocument &&
                focusedDocument->GetTop(&focusedContext) == S_OK &&
                focusedContext == pContext;
            // Every host query above can reenter focus/activation or replace
            // the composition. Never cancel a newer target on that old result.
            if (_tfClientId != TF_CLIENTID_NULL && _pThreadMgr == threadMgr &&
                _responseApplyQueue.CurrentEpoch() == engineEpoch &&
                _pendingUIGeneration == uiGeneration &&
                _IsCurrentComposition(composition) &&
                _IsComposingInContext(pContext)) {
              if (abortEngine)
                _AbortComposition(true, kFocusLossClearTimeoutMs, TF_ES_ASYNC);
              else
                _EndComposition(pContext, true);
            }
          }
        }
        tfSelection.range->Release();
      }
    }
  }

  /* text modification? */
  if (pEditRecord->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0,
                                             &pEnumTextChanges) == S_OK) {
    if (pEnumTextChanges->Next(1, &pRange, NULL) == S_OK)
      pRange->Release();
    pEnumTextChanges->Release();
  }

  // TF_E_LOCKED is only meaningful while another text service owns this
  // context. Retry after that READWRITE session has ended instead of burning
  // the bounded retry budget recursively inside the same lock stack.
  if (_responseApplyQueue.ConsumeRetryRequest())
    _ScheduleNextResponseApply(false);
  if (_handoffWaitPauseToken != 0 && _handoffWaitContext == pContext)
    _ResumeHandoffWait();
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnLayoutChange(ITfContext* pContext,
                                       TfLayoutCode lcode,
                                       ITfContextView* pContextView) {
  if (!_IsComposingInContext(pContext))
    return S_OK;

  if (pContext != _pTextEditSinkContext)
    return S_OK;

  if (lcode == TF_LC_CHANGE) {
    _UpdateCompositionWindow(pContext, _pendingUIGeneration);
  }
  return S_OK;
}

BOOL WeaselTSF::_InitTextEditSink(com_ptr<ITfDocumentMgr> pDocMgr) {
  com_ptr<ITfSource> pSource;

  if (_dwTextEditSinkCookie != TF_INVALID_COOKIE ||
      _dwTextLayoutSinkCookie != TF_INVALID_COOKIE) {
    if (_pTextEditSinkContext)
      _pTextEditSinkContext->QueryInterface(&pSource);
    if (pSource != nullptr) {
      if (_dwTextEditSinkCookie != TF_INVALID_COOKIE)
        pSource->UnadviseSink(_dwTextEditSinkCookie);
      if (_dwTextLayoutSinkCookie != TF_INVALID_COOKIE)
        pSource->UnadviseSink(_dwTextLayoutSinkCookie);
    }
    _pTextEditSinkContext = nullptr;
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;
    _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  }
  if (pDocMgr == NULL)
    return TRUE;

  if (pDocMgr->GetTop(&_pTextEditSinkContext) != S_OK)
    return FALSE;

  if (_pTextEditSinkContext == NULL)
    return TRUE;

  pSource.Release();
  if (_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void**)&pSource) !=
      S_OK) {
    _pTextEditSinkContext = nullptr;
    return FALSE;
  }

  // OnEndEdit is the retry trigger after TF_E_LOCKED. Without the text-edit
  // sink the response FIFO can stall indefinitely, so it is required.
  if (pSource->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink*)this,
                          &_dwTextEditSinkCookie) != S_OK) {
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;
    _pTextEditSinkContext = nullptr;
    return FALSE;
  }

  // Layout notifications only improve candidate positioning. Keep text input
  // functional when a host does not expose ITfTextLayoutSink.
  if (pSource->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink*)this,
                          &_dwTextLayoutSinkCookie) != S_OK) {
    _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  }

  return TRUE;
}
