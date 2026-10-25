#pragma once

#include "Globals.h"
#include "KeyEventPairState.h"
#include "ResponseApply.h"
#include "ResponseApplyQueue.h"
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <ResponseParser.h>

class CCandidateList;
class CLangBarItemButton;
class CCompartmentEventSink;
class WeaselTSFTestAccess;

class WeaselTSF : public ITfTextInputProcessorEx,
                  public ITfThreadMgrEventSink,
                  public ITfTextEditSink,
                  public ITfTextLayoutSink,
                  public ITfKeyEventSink,
                  public ITfCompositionSink,
                  public ITfThreadFocusSink,
                  public ITfActiveLanguageProfileNotifySink,
                  public ITfEditSession,
                  public ITfDisplayAttributeProvider {
 public:
  WeaselTSF();
  ~WeaselTSF();

  /* IUnknown */
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  /* ITfTextInputProcessor */
  STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId);
  STDMETHODIMP Deactivate();

  /* ITfTextInputProcessorEx */
  STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr,
                          TfClientId tfClientId,
                          DWORD dwFlags);

  /* ITfThreadMgrEventSink */
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr);
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr);
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                          ITfDocumentMgr* pDocMgrPrevFocus);
  STDMETHODIMP OnPushContext(ITfContext* pContext);
  STDMETHODIMP OnPopContext(ITfContext* pContext);

  /* ITfTextEditSink */
  STDMETHODIMP OnEndEdit(ITfContext* pic,
                         TfEditCookie ecReadOnly,
                         ITfEditRecord* pEditRecord);

  /* ITfTextLayoutSink */
  STDMETHODIMP OnLayoutChange(ITfContext* pContext,
                              TfLayoutCode lcode,
                              ITfContextView* pContextView);

  /* ITfKeyEventSink */
  STDMETHODIMP OnSetFocus(BOOL fForeground);
  STDMETHODIMP OnTestKeyDown(ITfContext* pContext,
                             WPARAM wParam,
                             LPARAM lParam,
                             BOOL* pfEaten);
  STDMETHODIMP OnKeyDown(ITfContext* pContext,
                         WPARAM wParam,
                         LPARAM lParam,
                         BOOL* pfEaten);
  STDMETHODIMP OnTestKeyUp(ITfContext* pContext,
                           WPARAM wParam,
                           LPARAM lParam,
                           BOOL* pfEaten);
  STDMETHODIMP OnKeyUp(ITfContext* pContext,
                       WPARAM wParam,
                       LPARAM lParam,
                       BOOL* pfEaten);
  STDMETHODIMP OnPreservedKey(ITfContext* pContext,
                              REFGUID rguid,
                              BOOL* pfEaten);

  // ITfThreadFocusSink
  STDMETHODIMP OnSetThreadFocus();
  STDMETHODIMP OnKillThreadFocus();

  /* ITfCompositionSink */
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                       ITfComposition* pComposition);

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

  /* ITfActiveLanguageProfileNotifySink */
  STDMETHODIMP OnActivated(REFCLSID clsid,
                           REFGUID guidProfile,
                           BOOL isActivated);

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(
      __RPC__deref_out_opt IEnumTfDisplayAttributeInfo** ppEnum);
  STDMETHODIMP GetDisplayAttributeInfo(
      __RPC__in REFGUID guidInfo,
      __RPC__deref_out_opt ITfDisplayAttributeInfo** ppInfo);

  ///* ITfCompartmentEventSink */
  // STDMETHODIMP OnChange(_In_ REFGUID guid);

  /* Compartments */
  BOOL _IsKeyboardDisabled();
  BOOL _IsKeyboardOpen();
  HRESULT _SetKeyboardOpen(BOOL fOpen);
  // 在 OnChange 通知内对同一 compartment 的 SetValue 会被 TSF 以
  // E_UNEXPECTED 拒绝（MSDN《ITfCompartment::SetValue》），强制重开须延迟
  // 到通知返回之后：_RequestKeyboardOpenDeferred 借异步编辑会话做延迟
  // 载体，会话内执行 _ApplyDeferredKeyboardOpen。自写同步触发的下一次
  // OnChange 由 _fSuppressOpenCloseSelfWrite 跳过，避免 ascii_mode 被
  // 强制重开翻转回去
  HRESULT _RequestKeyboardOpenDeferred();
  HRESULT _ApplyDeferredKeyboardOpen(UINT64 generation,
                                     UINT64 requestSerial,
                                     ITfContext* requestContext);
  static LRESULT CALLBACK _KeyboardOpenWindowProc(HWND hwnd,
                                                  UINT message,
                                                  WPARAM wParam,
                                                  LPARAM lParam);
  bool _EnsureKeyboardOpenWindow();
  void _DestroyKeyboardOpenWindow() noexcept;
  void _CancelKeyboardOpenRequest() noexcept;
  bool _ScheduleKeyboardOpenRetry() noexcept;
  void _OnKeyboardOpenTimer();
  void _CommitDeferredKeyboardOpen();
  HRESULT _GetCompartmentDWORD(DWORD& value, const GUID guid);
  HRESULT _SetCompartmentDWORD(const DWORD& value, const GUID guid);

  /* Composition */
  struct ResponseApplyRequest {
    com_ptr<ITfContext> context;
    weasel::tsf::ResponseApplySnapshot snapshot;
    UINT64 responseSerial = 0;
  };

  void _StartComposition(com_ptr<ITfContext> pContext,
                         BOOL fCUASWorkaroundEnabled,
                         UINT64 pendingUIGeneration = 0);
  HRESULT _StartCompositionLocked(TfEditCookie ec,
                                  com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled,
                                  UINT64 pendingUIGeneration = 0);
  void _EndComposition(com_ptr<ITfContext> pContext,
                       BOOL clear,
                       BOOL endUI = TRUE);
  HRESULT _EndCompositionLocked(TfEditCookie ec,
                                com_ptr<ITfContext> pContext,
                                com_ptr<ITfComposition> pComposition,
                                BOOL clear,
                                BOOL endUI);
  BOOL _ShowInlinePreedit(com_ptr<ITfContext> pContext,
                          const std::shared_ptr<weasel::Context> context);
  HRESULT _ShowInlinePreeditLocked(
      TfEditCookie ec,
      com_ptr<ITfContext> pContext,
      com_ptr<ITfComposition> pComposition,
      const std::shared_ptr<weasel::Context>& context,
      UINT64 pendingUIGeneration);
  HRESULT _InsertTextLocked(TfEditCookie ec,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            const std::wstring& text);
  void _UpdateComposition(com_ptr<ITfContext> pContext);
  // Variant for call sites that had to parse the response before scheduling
  // TSF apply (candidate confirmation may apply synchronously).
  void _UpdateComposition(com_ptr<ITfContext> pContext,
                          UINT64 previously_parsed_serial);
  bool _ConsumeResponse(LPWSTR buffer, DWORD length);
  void _ConsumeResponseIfFresh();
  HRESULT _ApplyResponseSnapshot(TfEditCookie ec,
                                 com_ptr<ITfContext> pContext,
                                 weasel::tsf::ResponseApplySnapshot snapshot);
  void _EnqueueResponseApply(com_ptr<ITfContext> pContext,
                             weasel::tsf::ResponseApplySnapshot snapshot,
                             UINT64 responseSerial);
  void _ScheduleNextResponseApply(bool allowSynchronous);
  void _CompleteResponseApply();
  BOOL _IsComposing();
  BOOL _IsComposingInContext(ITfContext* pContext);
  BOOL _IsCurrentComposition(ITfComposition* pComposition);
  void _SetComposition(com_ptr<ITfComposition> pComposition,
                       com_ptr<ITfContext> pContext);
  void _SetCompositionPosition(const RECT& rc, UINT64 pendingUIGeneration = 0);
  BOOL _UpdateCompositionWindow(com_ptr<ITfContext> pContext,
                                UINT64 pendingUIGeneration = 0);
  UINT64 _QueuePendingUI(const std::shared_ptr<weasel::Context>& context,
                         const weasel::Status& status);
  void _InvalidatePendingUI();
  bool _IsPendingUIGenerationCurrent(UINT64 generation) const;
  bool _TakePendingUI(UINT64 generation,
                      std::shared_ptr<weasel::Context>& context,
                      weasel::Status& status);
  // Publish UI data only for the extent request that owns this generation.
  void _FlushPendingUI(UINT64 pendingUIGeneration);
  void _FinalizeComposition();
  void _WaitForHandoffEdit(com_ptr<ITfContext> context,
                           com_ptr<ITfComposition> composition);
  void _ResumeHandoffWait();
  void _CancelHandoffWait();
  void _AbortComposition(bool clear = true, DWORD clear_timeout_ms = 0);

  /* Language bar */
  HWND _GetFocusedContextWindow();
  void _HandleLangBarMenuSelect(UINT wID);

  /* IPC */
  bool _EnsureServerConnected();

  /* UI */
  void _UpdateUI(const weasel::Context& ctx, const weasel::Status& status);
  void _StartUI(com_ptr<ITfContext> pContext = nullptr);
  void _EndUI();
  void _RenewCandidateUI(com_ptr<ITfContext> pContext,
                         UINT64 pendingUIGeneration,
                         const weasel::Context& ctx,
                         const weasel::Status& status);
  void _ShowUI();
  void _HideUI();
  // A confirmed candidate selection with an unapplyable TSF response must
  // retire its old UIElement instead of leaving Finalize permanently locked.
  void _RetireUnappliedCandidateFinalization(ITfContext* context,
                                             UINT64 responseSerial);
  com_ptr<ITfContext> _GetUIContextDocument();

  /* Display Attribute */
  void _ClearCompositionDisplayAttributes(
      TfEditCookie ec,
      _In_ ITfContext* pContext,
      _In_opt_ ITfComposition* pComposition);
  BOOL _SetCompositionDisplayAttributes(TfEditCookie ec,
                                        _In_ ITfContext* pContext,
                                        ITfRange* pRangeComposition);
  BOOL _InitDisplayAttributeGuidAtom();

  com_ptr<ITfThreadMgr> _GetThreadMgr() { return _pThreadMgr; }
  void HandleUICallback(size_t* const sel,
                        size_t* const hov,
                        bool* const next,
                        bool* const scroll_next);

 private:
  friend class WeaselTSFTestAccess;
  friend class CCandidateList;

  // A healthy physical key should never let a wedged server pin the host
  // callback for the pipe's one-second default. This only changes the wait
  // bound; the successful hot path performs the same single IPC transaction.
  static constexpr DWORD kKeyEventTransactionTimeoutMs = 100;

  // Losing focus runs synchronously inside a host callback. Keep only this
  // composition-clear IPC on a short budget; ordinary clears retain 3000 ms.
  static constexpr DWORD kFocusLossClearTimeoutMs = 100;

  // Keep transport discovery/reconnect on the physical-key path very short so
  // a missing or dead server cannot pin the host UI thread.
  static constexpr DWORD kFastTransportRecoveryBudgetMs = 20;

  // Creating/re-emitting a Rime session is a business operation, not a pipe
  // liveness probe: AddSession may create a Rime session, read status/schema,
  // load style, and refresh UI. Keep the historically working one-second bound
  // for activation and invalid-session rebuilds instead of forcing that work
  // through the 20 ms transport budget.
  static constexpr DWORD kSessionEstablishmentTimeoutMs = 1000;

  /* ui callback functions private */
  weasel::CandidateSelectResult _SelectCandidateOnCurrentPage(
      size_t index,
      com_ptr<ITfContext> context);
  void _HandleMouseHoverEvent(const size_t index);
  void _HandleMousePageEvent(bool* const nextPage, bool* const scrollNextPage);
  /* TSF Related */
  BOOL _InitThreadMgrEventSink();
  void _UninitThreadMgrEventSink();
  // ITfThreadFocusSink
  BOOL _InitThreadFocusSink();
  void _UninitThreadFocusSink();
  DWORD _dwThreadFocusSinkCookie;

  BOOL _InitTextEditSink(com_ptr<ITfDocumentMgr> pDocMgr);

  enum class KeyDispatchResult {
    Handled,
    Unhandled,
    // The server may already have processed the physical key. Do not replay it
    // and do not let the host process the same key a second time.
    AmbiguousConsumed,
  };

  BOOL _InitKeyEventSink();
  void _UninitKeyEventSink();
  void _ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
  KeyDispatchResult _ProcessKeyEventWithRecovery(
      const weasel::KeyEvent& keyEvent,
      ULONGLONG recovery_deadline = 0);

  BOOL _InitPreservedKey();
  void _UninitPreservedKey();

  BOOL _InitLanguageBar();
  void _UninitLanguageBar();
  void _UpdateLanguageBar(weasel::Status stat);
  void _ShowLanguageBar(BOOL show);
  void _EnableLanguageBar(BOOL enable);

  BOOL _InsertText(com_ptr<ITfContext> pContext, const std::wstring& ext);

  void _DeleteCandidateList();

  BOOL _InitCompartment();
  void _UninitCompartment();
  HRESULT _HandleCompartment(REFGUID guidCompartment);

  // K21：ToggleImeOnOpenClose 注册表值仅 WeaselSetup 写入，按 TTL 限频
  // 重读，免去每次线程焦点切换的注册表访问
  void _RefreshToggleImeOnOpenClose();
  ULONGLONG _toggleImeReadTick = 0;

  // K23c：_IsKeyboardDisabled 的禁用态缓存。KEYBOARD_DISABLED/
  // EMPTYCONTEXT 是 context 级 compartment，sink 挂在焦点 top context
  // 上，值变化置脏；context 变化（含 push/pop 改变 GetTop）时按身份
  // 比对重挂。advise 失败则不缓存，退回逐键查询
  HRESULT _OnKeyboardDisabledCompartmentChange(REFGUID guidCompartment);
  void _RetargetKeyboardDisabledSinks(com_ptr<ITfContext> pContext);

  void _Reconnect();
  bool _EnsureServerConnectedUntil(ULONGLONG recovery_deadline);
  void _ScheduleServerRecovery() noexcept;
  std::wstring _GetRootDir();
  // consecutive failed reconnect attempts (per instance, was a shared
  // file-static raced across threads and TIP instances)
  unsigned int _reconnectRetry = 0;

  bool isImmersive() const {
    return (_activateFlags & TF_TMF_IMMERSIVEMODE) != 0;
  }

  com_ptr<ITfThreadMgr> _pThreadMgr;
  TfClientId _tfClientId;
  DWORD _dwThreadMgrEventSinkCookie;

  com_ptr<ITfContext> _pTextEditSinkContext;
  DWORD _dwTextEditSinkCookie, _dwTextLayoutSinkCookie;
  BYTE _lpbKeyState[256];
  // OnTestKey*/OnKey* 配对状态。Test-vs-Test 使用 Windows message time
  // 区分同一消息的重复探测与后续相同 vkey 的真实输入；Test-vs-Key 仍只按
  // vkey 配对，因为 CUAS/IMM32 bridge 不保证两次回调的 lParam 完全一致。
  weasel::tsf::PendingTestKeyState _testKeyDownPending;
  weasel::tsf::PendingTestKeyState _testKeyUpPending;
  // Caps Lock 模拟的跨按键状态。键事件 sink 按 thread manager 各持一份，
  // 实例化即按线程隔离（原文件级 static 被同进程多实例/多线程共享）
  weasel::KeyEvent _prevKeyEvent;
  BOOL _prevfEaten = FALSE;
  int _keyCountToSimulate = 0;

  com_ptr<ITfContext> _pEditSessionContext;

  com_ptr<CCompartmentEventSink> _pKeyboardCompartmentSink;
  com_ptr<CCompartmentEventSink> _pConvertionCompartmentSink;

  // 自写 OPENCLOSE 的抑制标志：_ApplyDeferredKeyboardOpen 置位期间，
  // _HandleCompartment 跳过该次自写触发的通知（见 Compartments 注释）
  BOOL _fSuppressOpenCloseSelfWrite = FALSE;
  UINT64 _keyboardOpenGeneration = 0;
  // At most one deferred recovery for this TIP activation: stale callbacks
  // never consume a later request or toggle the mode twice.
  UINT64 _keyboardOpenRequestSerial = 0;
  bool _keyboardOpenPending = false;
  ULONGLONG _keyboardOpenPendingSinceTick = 0;
  unsigned int _keyboardOpenRetries = 0;
  bool _keyboardOpenCommitPending = false;
  com_ptr<ITfContext> _keyboardOpenRequestContext;
  HWND _keyboardOpenWindow = nullptr;
  ATOM _keyboardOpenWindowClassAtom = 0;
  HMODULE _keyboardOpenWindowModule = nullptr;
  std::wstring _keyboardOpenWindowClassName;

  // 禁用态缓存（K23c）：_pDisabledCacheContext 为空表示不缓存
  com_ptr<CCompartmentEventSink> _pKeyboardDisabledSink;
  com_ptr<CCompartmentEventSink> _pEmptyContextSink;
  com_ptr<ITfContext> _pDisabledCacheContext;
  BOOL _fKeyboardDisabled = FALSE;
  BOOL _fKeyboardDisabledDirty = TRUE;

  com_ptr<ITfComposition> _pComposition;
  com_ptr<ITfContext> _pCompositionContext;
  com_ptr<ITfContext> _pCandidateUIContext;

  com_ptr<CLangBarItemButton> _pLangBarButton;

  com_ptr<CCandidateList> _cand;

  LONG _cRef;  // COM ref count

  /* CUAS Candidate Window Position Workaround */
  BOOL _fCUASWorkaroundTested, _fCUASWorkaroundEnabled;

  /* Weasel Related */
  weasel::Client m_client;
  DWORD _activateFlags;

  /* IME status */
  weasel::Status _status;

  /* 已解析、待编辑会话应用的服务器应答；解析发生在按键/UI回调线程内
   * （应答缓冲在下一次 Transact 前有效），会话可能异步排队、晚于解析 */
  std::wstring _pendingCommit;  // commit 是事件流：应用前按序累积
  std::shared_ptr<weasel::Context> _context =
      std::make_shared<weasel::Context>();
  weasel::Config _config;
  UINT64 _parsedSerial = 0;
  weasel::tsf::SerialApplyQueue<std::shared_ptr<ResponseApplyRequest>>
      _responseApplyQueue;
  // A dropped asynchronous handoff waits for an old-context edit notification.
  // The barrier is not a second TSF composition or a cleanup edit session.
  UINT64 _handoffWaitPauseToken = 0;
  com_ptr<ITfContext> _handoffWaitContext;
  // 复用的响应解析器：deserializer 注册表只建一次（Require 幂等），
  // 每次解析前重指目标；目标指针仅在 _ConsumeResponse 执行期间有效
  weasel::ResponseParser _parser{nullptr, nullptr, nullptr, nullptr, nullptr};

  // guidatom for the display attibute. 0 = RegisterGUID failed or not yet
  // run; guards the SetValue in _SetCompositionDisplayAttributes.
  TfGuidAtom _gaDisplayAttributeInput = 0;
  BOOL _committed = false;
  BOOL _isToOpenClose = false;
  std::shared_ptr<weasel::Context> _pendingUIContext;
  weasel::Status _pendingUIStatus;
  UINT64 _pendingUIGeneration = 0;
  bool _hasPendingUI = false;
};
