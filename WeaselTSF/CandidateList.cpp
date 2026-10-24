#include "stdafx.h"

#include "WeaselTSF.h"
#include "BstrUtil.h"
#include "CandidateList.h"
#include "AsyncUiPolicy.h"
#include <KeyEvent.h>
#include <math.h>

using namespace std;
using namespace weasel;

CCandidateList::DllLifetimeLease::DllLifetimeLease() {
  DllAddRef();
}

CCandidateList::DllLifetimeLease::~DllLifetimeLease() {
  DllRelease();
}

CCandidateList::CCandidateList(WeaselTSF* pTextService)
    : _ui(make_unique<UI>()), _tsf(pTextService), _pbShow(TRUE) {
  _cRef = 1;
}

CCandidateList::~CCandidateList() {
  DetachOwner();
}

void CCandidateList::DetachOwner() {
  if (!_tsf)
    return;
  // EndUIElement while the original TIP/thread manager still exists, then
  // dispose the panel before severing the final back-reference.
  DestroyAll();
  _ui->SetUICallBack({});
  _pContextDocument = nullptr;
  _tsf = nullptr;
  // External UIElement consumers may still call this COM interface.
  _ui->ctx() = Context{};
  _ui->status() = Status{};
}

STDMETHODIMP CCandidateList::QueryInterface(REFIID riid, void** ppvObj) {
  if (ppvObj == nullptr) {
    return E_INVALIDARG;
  }

  *ppvObj = nullptr;

  if (IsEqualIID(riid, IID_ITfUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
    *ppvObj = (ITfCandidateListUIElementBehavior*)this;
  } else if (IsEqualIID(riid, IID_IUnknown) ||
             IsEqualIID(riid,
                        __uuidof(ITfIntegratableCandidateListUIElement))) {
    *ppvObj = (ITfIntegratableCandidateListUIElement*)this;
  }

  if (*ppvObj) {
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCandidateList::AddRef(void) {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) CCandidateList::Release(void) {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0) {
    delete this;
  }

  return cr;
}

STDMETHODIMP CCandidateList::GetDescription(BSTR* pbstr) {
  // BSTR out params are owned (and freed) by the caller: hand out a fresh
  // allocation every time instead of a shared static BSTR that would be
  // double-freed by the second caller.
  if (pbstr == nullptr)
    return E_POINTER;
  *pbstr = SysAllocString(L"Candidate List");
  return (*pbstr != nullptr) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CCandidateList::GetGUID(GUID* pguid) {
  /// 36c3c795-7159-45aa-ab12-30229a51dbd3
  *pguid = {0x36c3c795,
            0x7159,
            0x45aa,
            {0xab, 0x12, 0x30, 0x22, 0x9a, 0x51, 0xdb, 0xd3}};
  return S_OK;
}

STDMETHODIMP CCandidateList::Show(BOOL showCandidateWindow) {
  if (!_tsf)
    return E_UNEXPECTED;
  if (showCandidateWindow)
    _ui->Show();
  else
    _ui->Hide();
  return S_OK;
}

STDMETHODIMP CCandidateList::IsShown(BOOL* pIsShow) {
  if (!pIsShow)
    return E_POINTER;
  *pIsShow = _tsf && _ui->IsShown();
  return S_OK;
}

STDMETHODIMP CCandidateList::GetUpdatedFlags(DWORD* pdwFlags) {
  if (!pdwFlags)
    return E_INVALIDARG;

  *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
              TF_CLUIE_STRING | TF_CLUIE_CURRENTPAGE;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetDocumentMgr(ITfDocumentMgr** ppdim) {
  if (!ppdim)
    return E_POINTER;
  *ppdim = nullptr;
  if (!_pContextDocument)
    return E_FAIL;
  return _pContextDocument->GetDocumentMgr(ppdim);
}

STDMETHODIMP CCandidateList::GetCount(UINT* pCandidateCount) {
  if (!pCandidateCount)
    return E_POINTER;
  *pCandidateCount =
      _tsf ? static_cast<UINT>(_ui->ctx().cinfo.candies.size()) : 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetSelection(UINT* pSelectedCandidateIndex) {
  if (!pSelectedCandidateIndex)
    return E_POINTER;
  *pSelectedCandidateIndex = _tsf ? _ui->ctx().cinfo.highlighted : 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetString(UINT uIndex, BSTR* pbstr) {
  if (!pbstr)
    return E_POINTER;
  *pbstr = nullptr;
  if (!_tsf)
    return E_UNEXPECTED;
  auto& cinfo = _ui->ctx().cinfo;
  if (uIndex >= cinfo.candies.size())
    return E_INVALIDARG;

  auto& str = cinfo.candies[uIndex].str;
  *pbstr = AllocBstr(str);

  return S_OK;
}

STDMETHODIMP CCandidateList::GetPageIndex(UINT* pIndex,
                                          UINT uSize,
                                          UINT* puPageCnt) {
  if (!puPageCnt)
    return E_INVALIDARG;
  *puPageCnt = 1;
  if (pIndex) {
    if (uSize < *puPageCnt) {
      return E_INVALIDARG;
    }
    *pIndex = 0;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::SetPageIndex(UINT* pIndex, UINT uPageCnt) {
  if (!pIndex)
    return E_INVALIDARG;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetCurrentPage(UINT* puPage) {
  *puPage = 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::SetSelection(UINT nIndex) {
  if (!_tsf)
    return E_UNEXPECTED;
  if ((!_uiStarted && !_registration_in_progress) || !_pContextDocument ||
      _finalization_started)
    return E_FAIL;
  auto& cinfo = _ui->ctx().cinfo;
  // Reject indices without a candidate: highlighted feeds fixed-size arrays
  // downstream (GetCandidateRect). Leaving the previous selection intact on
  // a transient count mismatch is safer than writing a bogus index.
  if (nIndex >= cinfo.candies.size())
    return E_INVALIDARG;
  cinfo.highlighted = static_cast<int>(nIndex);
  return S_OK;
}

STDMETHODIMP CCandidateList::Finalize(void) {
  if (!_tsf)
    return E_UNEXPECTED;
  // A host can call Finalize synchronously from BeginUIElement. Until
  // registration returns, no valid UIElement ID exists to end and the
  // result of an eventual selection cannot be reported to this caller.
  // Reject early confirmation instead of queuing it and returning S_OK.
  if (_registration_in_progress)
    return E_FAIL;
  if (!_uiStarted || !_pContextDocument || _finalization_started ||
      _tsf->_pCandidateUIContext != _pContextDocument)
    return E_FAIL;

  // An externally retained UIElement must not commit into a different
  // focused document after its original composition lost focus.
  com_ptr<ITfThreadMgr> thread_mgr = _tsf->_GetThreadMgr();
  com_ptr<ITfDocumentMgr> focused_document;
  com_ptr<ITfContext> focused_context;
  if (!thread_mgr || FAILED(thread_mgr->GetFocus(&focused_document)) ||
      !focused_document || FAILED(focused_document->GetTop(&focused_context)) ||
      focused_context != _pContextDocument)
    return E_FAIL;

  const auto& cinfo = _ui->ctx().cinfo;
  if (cinfo.highlighted < 0 ||
      static_cast<size_t>(cinfo.highlighted) >= cinfo.candies.size())
    return E_FAIL;

  // Set the guard before transport/UI updates: EndUIElement may reenter this
  // COM object, and one candidate generation must not commit twice.
  _finalization_started = true;
  const auto result = _tsf->_SelectCandidateOnCurrentPage(
      static_cast<size_t>(cinfo.highlighted), _pContextDocument);
  if (result == CandidateSelectResult::Selected) {
    Destroy();
    return S_OK;
  }
  if (result == CandidateSelectResult::NotDelivered ||
      result == CandidateSelectResult::Rejected) {
    // No candidate was selected. The host may correct its selection and retry.
    _finalization_started = false;
    return E_FAIL;
  }
  // The candidate may have been selected despite a lost response: do not
  // leave a permanently locked UI or permit the host to submit it twice.
  Destroy();
  return E_FAIL;
}

STDMETHODIMP CCandidateList::Abort(void) {
  if (!_tsf)
    return E_UNEXPECTED;
  _tsf->_AbortComposition(true);
  Destroy();
  return S_OK;
}

STDMETHODIMP CCandidateList::SetIntegrationStyle(GUID guidIntegrationStyle) {
  // We do not implement search-box keyboard integration. Advertising support
  // would cause the host to route keys into an unimplemented OnKeyDown.
  return _tsf ? E_NOTIMPL : E_UNEXPECTED;
}

STDMETHODIMP CCandidateList::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle) {
  if (!ptfSelectionStyle)
    return E_POINTER;
  *ptfSelectionStyle = _selectionStyle;
  return S_OK;
}

STDMETHODIMP CCandidateList::OnKeyDown(WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL* pIsEaten) {
  if (!pIsEaten)
    return E_POINTER;
  // No integrated key handler is installed; leave navigation/confirmation to
  // the host rather than claiming to have processed the key.
  *pIsEaten = FALSE;
  return _tsf ? S_OK : E_UNEXPECTED;
}

STDMETHODIMP CCandidateList::ShowCandidateNumbers(BOOL* pIsShow) {
  *pIsShow = TRUE;
  return S_OK;
}

STDMETHODIMP CCandidateList::FinalizeExactCompositionString() {
  // Unsupported: do not mutate composition state while reporting E_NOTIMPL.
  return _tsf ? E_NOTIMPL : E_UNEXPECTED;
}

void CCandidateList::UpdateUI(const Context& ctx, const Status& status) {
  if (!_tsf)
    return;
  if (_ui->style().inline_preedit) {
    _ui->style().client_caps |= weasel::INLINE_PREEDIT_CAPABLE;
  } else {
    _ui->style().client_caps &= ~weasel::INLINE_PREEDIT_CAPABLE;
  }

  _ui->Update(ctx, status);
  if (_uiStarted)
    _UpdateUIElement();

  bool targetFocused = false;
  if (_pContextDocument) {
    com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
    com_ptr<ITfDocumentMgr> pDocumentMgr;
    com_ptr<ITfContext> pContext;
    targetFocused =
        pThreadMgr && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) &&
        pDocumentMgr && SUCCEEDED(pDocumentMgr->GetTop(&pContext)) &&
        pContext == _pContextDocument;
  }

  Show(weasel::tsf::ShouldShowCandidateUI(status.composing, _pbShow != FALSE,
                                          targetFocused));
}

void CCandidateList::UpdateInputPosition(RECT const& rc) {
  if (_tsf)
    _ui->UpdateInputPosition(rc);
}

// Candidate UI lifecycle (state machine):
//
//   StartUI:     !_uiStarted -> begun. BeginUIElement registers this object
//                with the thread's UIElementMgr (id stored in uiid); if the
//                host shows UI elements, _MakeUIWindow creates the panel.
//                No-op while _uiStarted.
//   EndUI:       _uiStarted -> !begun. EndUIElement(uiid) plus
//                _DisposeUIWindow; idempotent.
//   Destroy:     composition episode ended (abort / focus loss / profile
//                deactivation): EndUI + hide. The element must end here so
//                the next composition can StartUI and rebuild the window.
//   DestroyAll:  Deactivate teardown: Destroy plus full resource release.
//
// Invariants:
//   _uiStarted == true  <=>  an element registered under uiid is begun
//   panel window exists only between _MakeUIWindow and _DisposeUIWindow
//     (weasel::UI ops on a disposed window are guarded no-ops)
void CCandidateList::Destroy() {
  EndUI();
  Show(FALSE);
  _DisposeUIWindow();
}

void CCandidateList::DestroyAll() {
  // End the UIElement so the UIElementMgr drops its reference; without it
  // the element outlives WeaselTSF while keeping this object alive.
  EndUI();
  Show(FALSE);
  _DisposeUIWindowAll();
}
UIStyle& CCandidateList::style() {
  // return _ui->style();
  return _style;
}

HWND CCandidateList::_GetActiveWnd() {
  if (!_tsf)
    return nullptr;
  com_ptr<ITfContextView> pContextView;
  HWND w = NULL;

  if (_pContextDocument &&
      SUCCEEDED(_pContextDocument->GetActiveView(&pContextView))) {
    pContextView->GetWnd(&w);
  }

  // Some bridged applications expose no TSF view HWND. Falling back to the
  // Win32 focus window is safe only while that focus still belongs to our
  // target context; never retarget an asynchronous response to another doc.
  if (w == NULL && _pContextDocument) {
    com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
    com_ptr<ITfDocumentMgr> pDocumentMgr;
    com_ptr<ITfContext> pContext;
    if (pThreadMgr && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) &&
        pDocumentMgr && SUCCEEDED(pDocumentMgr->GetTop(&pContext)) &&
        pContext == _pContextDocument) {
      w = ::GetFocus();
    }
  }
  return w;
}

HRESULT CCandidateList::_UpdateUIElement() {
  if (!_tsf)
    return E_UNEXPECTED;
  // No element registered: uiid is not valid (never begun, or already ended).
  // UpdateUI runs on every keystroke, including the ones before StartUI and
  // after EndUI/Destroy.
  if (!_uiStarted)
    return S_OK;

  HRESULT hr = S_OK;

  com_ptr<ITfUIElementMgr> pUIElementMgr;
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (nullptr == pThreadMgr) {
    return S_OK;
  }
  hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void**)&pUIElementMgr);

  if (hr == S_OK) {
    pUIElementMgr->UpdateUIElement(uiid);
  }

  return S_OK;
}

void CCandidateList::StartUI(com_ptr<ITfContext> pContext) {
  if (!_tsf || _uiStarted)
    return;

  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (!pThreadMgr || !pContext)
    return;

  com_ptr<ITfUIElementMgr> pUIElementMgr;
  auto hr = pThreadMgr->QueryInterface(&pUIElementMgr);
  if (FAILED(hr))
    return;

  if (pUIElementMgr == NULL)
    return;

  if (!_ui->uiCallback())
    _ui->SetUICallBack([this](size_t* const sel, size_t* const hov,
                              bool* const next, bool* const scroll_next) {
      if (_tsf)
        _tsf->HandleUICallback(sel, hov, next, scroll_next);
    });

  // BeginUIElement may synchronously query GetDocumentMgr, so bind the exact
  // response/composition target before registering the UIElement.
  _pContextDocument = pContext;
  _registration_in_progress = true;
  _finalization_started = false;
  _pbShow = TRUE;

  const HRESULT begin_result =
      pUIElementMgr->BeginUIElement(this, &_pbShow, &uiid);
  _registration_in_progress = false;
  if (FAILED(begin_result)) {
    _pContextDocument = nullptr;
    return;
  }
  if (_pContextDocument != pContext) {
    // Focus/teardown may have reentered during BeginUIElement.
    pUIElementMgr->EndUIElement(uiid);
    return;
  }
  _uiStarted = true;
  // pUIElementMgr->UpdateUIElement(uiid);
  if (_pbShow) {
    _ui->style() = _style;
    _MakeUIWindow();
    if (!_ui_warmed_up && _ui->hwnd() != nullptr) {
      // Warm up render resources before first visible paint in TSF path.
      _ui->Refresh();
      _ui->Hide();
      _ui_warmed_up = true;
    }
  }
}

void CCandidateList::EndUI() {
  if (!_uiStarted) {
    _pContextDocument = nullptr;
    return;
  }

  // Host unregistration is best effort. Even if the UIElementMgr disappears
  // during a context/profile transition, the local lifetime must end so the
  // next composition can register a fresh element and owner window.
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf ? _tsf->_GetThreadMgr() : nullptr;
  if (pThreadMgr) {
    com_ptr<ITfUIElementMgr> emgr;
    if (SUCCEEDED(pThreadMgr->QueryInterface(&emgr)) && emgr != NULL)
      emgr->EndUIElement(uiid);
  }
  _uiStarted = false;
  _pContextDocument = nullptr;
  _DisposeUIWindow();
}

com_ptr<ITfContext> CCandidateList::GetContextDocument() {
  return _pContextDocument;
}

void CCandidateList::_DisposeUIWindow() {
  if (_ui == nullptr) {
    return;
  }

  _ui->Destroy();
}

void CCandidateList::_DisposeUIWindowAll() {
  if (_ui == nullptr) {
    return;
  }

  // call _ui->Destroy(true) to clean resources
  _ui->Destroy(true);
  _ui_warmed_up = false;
}

void CCandidateList::_MakeUIWindow() {
  HWND p = _GetActiveWnd();
  _ui->Create(p);
}

void WeaselTSF::_UpdateUI(const Context& ctx, const Status& status) {
  _cand->UpdateUI(ctx, status);
}

void WeaselTSF::_StartUI(com_ptr<ITfContext> pContext) {
  // BeginUIElement may synchronously invoke SetSelection/Finalize. Publish
  // ownership before the call, then reconcile with the actual UI state.
  _pCandidateUIContext = pContext;
  _cand->StartUI(pContext);
  _pCandidateUIContext = _cand->GetContextDocument();
}

void WeaselTSF::_EndUI() {
  _cand->EndUI();
  _pCandidateUIContext = nullptr;
}

void WeaselTSF::_ShowUI() {
  _cand->Show(TRUE);
}

void WeaselTSF::_HideUI() {
  _cand->Show(FALSE);
}

com_ptr<ITfContext> WeaselTSF::_GetUIContextDocument() {
  return _cand->GetContextDocument();
}

void WeaselTSF::_DeleteCandidateList() {
  _cand->Destroy();
  _pCandidateUIContext = nullptr;
}

CandidateSelectResult WeaselTSF::_SelectCandidateOnCurrentPage(
    size_t index,
    com_ptr<ITfContext> context) {
  if (!context || !m_client.IsSessionActive())
    return CandidateSelectResult::NotDelivered;

  const UINT64 previous_serial = m_client.ResponseSerial();
  const auto result = m_client.SelectCandidateOnCurrentPageEx(index);
  if (result == CandidateSelectResult::NotDelivered ||
      result == CandidateSelectResult::Rejected ||
      result == CandidateSelectResult::Ambiguous)
    return result;

  const UINT64 received_serial = m_client.ResponseSerial();
  if (received_serial == previous_serial)
    return CandidateSelectResult::Ambiguous;

  // Old servers don't report Rime's selection result. Only trust a legacy
  // body if it actually adds a new commit. New servers return the Rime result.
  bool has_new_commit = false;
  _UpdateComposition(context, &has_new_commit);
  if (_parsedSerial != received_serial ||
      (result == CandidateSelectResult::LegacyResponse && !has_new_commit))
    return CandidateSelectResult::Ambiguous;
  return CandidateSelectResult::Selected;
}

void WeaselTSF::_HandleMousePageEvent(bool* const nextPage,
                                      bool* const scrollNextPage) {
  // from scrolling event
  if (scrollNextPage) {
    if (_cand->style().paging_on_scroll)
      m_client.ChangePage(!(*scrollNextPage));
    else {
      UINT current_select = 0, cand_count = 0;
      _cand->GetSelection(&current_select);
      _cand->GetCount(&cand_count);
      bool is_reposition = _cand->GetIsReposition();
      int offset = *scrollNextPage ? 1 : -1;
      offset = offset * (is_reposition ? -1 : 1);
      int index = (int)current_select + offset;
      if (index >= 0 && index < (int)cand_count)
        m_client.HighlightCandidateOnCurrentPage((size_t)index);
      else {
        KeyEvent ke{0, 0};
        ke.keycode = (index < 0) ? ibus::Up : ibus::Down;
        m_client.ProcessKeyEvent(ke);
      }
    }
  } else {  // from click event
    m_client.ChangePage(!(*nextPage));
  }
  _UpdateComposition(_pEditSessionContext);
}

void WeaselTSF::_HandleMouseHoverEvent(const size_t index) {
  UINT current_select = 0;
  _cand->GetSelection(&current_select);

  if (index != current_select) {
    m_client.HighlightCandidateOnCurrentPage(index);
    _UpdateComposition(_pEditSessionContext);
  }
}

void WeaselTSF::HandleUICallback(size_t* const sel,
                                 size_t* const hov,
                                 bool* const next,
                                 bool* const scroll_next) {
  if (sel)
    _SelectCandidateOnCurrentPage(*sel, _pCandidateUIContext
                                            ? _pCandidateUIContext
                                            : _pEditSessionContext);
  else if (hov)
    _HandleMouseHoverEvent(*hov);
  else if (next || scroll_next)
    _HandleMousePageEvent(next, scroll_next);
}
