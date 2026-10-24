#include "stdafx.h"
#include "WeaselTSF.h"

static BOOL IsRangeCovered(TfEditCookie ec,
                           ITfRange* pRangeTest,
                           ITfRange* pRangeCover) {
  LONG lResult;

  if (pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult) !=
          S_OK ||
      lResult > 0)
    return FALSE;
  if (pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult) !=
          S_OK ||
      lResult < 0)
    return FALSE;
  return TRUE;
}

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
      /* if the caret moves out of composition range, stop the composition */
      TF_SELECTION tfSelection;
      ULONG cFetched;

      if (pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1,
                                 &tfSelection, &cFetched) == S_OK &&
          cFetched == 1) {
        ITfRange* pRangeComposition;
        if (_pComposition->GetRange(&pRangeComposition) == S_OK) {
          if (!IsRangeCovered(ecReadOnly, tfSelection.range, pRangeComposition))
            _EndComposition(pContext, true);
          pRangeComposition->Release();
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
  if (_abandonedHandoffPauseToken != 0 &&
      _abandonedHandoffContext == pContext)
    _ScheduleAbandonedHandoffCleanup();
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
