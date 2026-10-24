#pragma once
#include <WeaselUI.h>
#include "ctffunc.h"

class WeaselTSF;

class CCandidateList : public ITfIntegratableCandidateListUIElement,
                       public ITfCandidateListUIElementBehavior {
 public:
  CCandidateList(WeaselTSF* pTextService);
  ~CCandidateList();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void** ppvObj);
  STDMETHODIMP_(ULONG) AddRef(void);
  STDMETHODIMP_(ULONG) Release(void);

  // ITfUIElement
  STDMETHODIMP GetDescription(BSTR* pbstr);
  STDMETHODIMP GetGUID(GUID* pguid);
  STDMETHODIMP Show(BOOL showCandidateWindow);
  STDMETHODIMP IsShown(BOOL* pIsShow);

  // ITfCandidateListUIElement
  STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags);
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim);
  STDMETHODIMP GetCount(UINT* pCandidateCount);
  STDMETHODIMP GetSelection(UINT* pSelectedCandidateIndex);
  STDMETHODIMP GetString(UINT uIndex, BSTR* pbstr);
  STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt);
  STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt);
  STDMETHODIMP GetCurrentPage(UINT* puPage);

  // ITfCandidateListUIElementBehavior methods
  STDMETHODIMP SetSelection(UINT nIndex);
  STDMETHODIMP Finalize(void);
  STDMETHODIMP Abort(void);

  // ITfIntegratableCandidateListUIElement methods
  STDMETHODIMP SetIntegrationStyle(GUID guidIntegrationStyle);
  STDMETHODIMP GetSelectionStyle(
      _Out_ TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle);
  STDMETHODIMP OnKeyDown(_In_ WPARAM wParam,
                         _In_ LPARAM lParam,
                         _Out_ BOOL* pIsEaten);
  STDMETHODIMP ShowCandidateNumbers(_Out_ BOOL* pIsShow);
  STDMETHODIMP FinalizeExactCompositionString();

  /* Update */
  void UpdateUI(const weasel::Context& ctx, const weasel::Status& status);
  void UpdateInputPosition(RECT const& rc);
  void Destroy();
  void DestroyAll();
  // Unlink the non-owning TIP pointer before the host's final COM Release.
  // A detached candidate may remain externally referenced but cannot call
  // back into a destroyed WeaselTSF or retain UI window callbacks.
  void DetachOwner();
  bool HasOwner() const { return _tsf != nullptr; }
  void StartUI(com_ptr<ITfContext> pContext);
  void EndUI();
  bool IsUIless() const { return _uiStarted && !_pbShow; }

  com_ptr<ITfContext> GetContextDocument();
  bool GetIsReposition() {
    if (_ui)
      return _ui->GetIsReposition();
    else
      return false;
  }

  weasel::UIStyle& style();

 private:
  struct DllLifetimeLease {
    DllLifetimeLease();
    ~DllLifetimeLease();
  };

  // Declared first so its destructor runs last, after UI and COM members.
  DllLifetimeLease _dllLifetime;
  HWND _GetActiveWnd();
  HRESULT _UpdateUIElement();

  void _DisposeUIWindow();
  void _DisposeUIWindowAll();
  void _MakeUIWindow();

  std::unique_ptr<weasel::UI> _ui;
  DWORD _cRef;
  // Weak owner: DetachOwner invalidates this before WeaselTSF dies.
  // A COM strong reference here would create a cycle with WeaselTSF::_cand.
  WeaselTSF* _tsf;
  DWORD uiid = 0;
  TfIntegratableCandidateListSelectionStyle _selectionStyle =
      STYLE_ACTIVE_SELECTION;

  BOOL _pbShow;
  bool _uiStarted = false;
  bool _finalization_started = false;
  weasel::UIStyle _style;
  bool _ui_warmed_up = false;

  // Exact response/composition target. UIElement callbacks and the native
  // owner window must stay bound to this context even if thread focus changes
  // before an asynchronous response session actually executes.
  com_ptr<ITfContext> _pContextDocument;
};
