#include "stdafx.h"

#include <WeaselIPCData.h>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "LanguageBar.h"
#include "Compartment.h"
#include "Globals.h"

WeaselTSF::WeaselTSF() {
  _cRef = 1;

  _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;

  _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

  _fCUASWorkaroundTested = _fCUASWorkaroundEnabled = FALSE;

  // Attach (no AddRef): ~WeaselTSF's CComPtr release must be able to drop
  // the reference count to zero (a plain assignment would leak one).
  _cand.Attach(new CCandidateList(this));

  DllAddRef();
}

WeaselTSF::~WeaselTSF() {
  DllRelease();
}

STDMETHODIMP WeaselTSF::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfTextInputProcessor))
    *ppvObject = (ITfTextInputProcessor*)this;
  else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    *ppvObject = (ITfTextInputProcessorEx*)this;
  else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    *ppvObject = (ITfThreadMgrEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextEditSink))
    *ppvObject = (ITfTextEditSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
    *ppvObject = (ITfTextLayoutSink*)this;
  else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    *ppvObject = (ITfKeyEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfCompositionSink))
    *ppvObject = (ITfCompositionSink*)this;
  else if (IsEqualIID(riid, IID_ITfEditSession))
    *ppvObject = (ITfEditSession*)this;
  else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    *ppvObject = (ITfThreadFocusSink*)this;
  else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    *ppvObject = (ITfDisplayAttributeProvider*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) WeaselTSF::AddRef() {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) WeaselTSF::Release() {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0)
    delete this;

  return cr;
}

STDMETHODIMP WeaselTSF::Activate(ITfThreadMgr* pThreadMgr,
                                 TfClientId tfClientId) {
  return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDMETHODIMP WeaselTSF::Deactivate() {
  _InvalidatePendingUI();
  _responseApplyQueue.Reset();
  _pendingCommit.clear();
  m_client.EndSession();

  _InitTextEditSink(com_ptr<ITfDocumentMgr>());

  _UninitThreadMgrEventSink();

  _UninitKeyEventSink();
  _UninitPreservedKey();

  _UninitLanguageBar();

  _UninitCompartment();

  _UninitThreadFocusSink();

  // 停用期间 INPUTMODE_CONVERSION 可能被宿主/其他输入源改写，
  // 上次写入值缓存不再可信
  _conversionFlagsValid = false;

  // While the thread manager is still valid: DestroyAll ends the UIElement,
  // which makes the UIElementMgr drop its reference to the candidate list.
  _cand->DestroyAll();
  _pCandidateUIContext = nullptr;

  _pThreadMgr = NULL;

  _tfClientId = TF_CLIENTID_NULL;

  return S_OK;
}

STDMETHODIMP WeaselTSF::ActivateEx(ITfThreadMgr* pThreadMgr,
                                   TfClientId tfClientId,
                                   DWORD dwFlags) {
  LoadWeaselLogLevelFromRegistry();
  com_ptr<ITfDocumentMgr> pDocMgrFocus;
  _activateFlags = dwFlags;

  // Treat each TSF activation as a fresh response-apply epoch. This also
  // recovers cleanly from hosts that reactivate after a partial teardown.
  _responseApplyQueue.Reset();
  _pendingCommit.clear();
  _pCandidateUIContext = nullptr;

  _pThreadMgr = pThreadMgr;
  _tfClientId = tfClientId;

  if (!_InitThreadMgrEventSink())
    goto ExitError;

  if ((_pThreadMgr->GetFocus(&pDocMgrFocus) == S_OK) &&
      (pDocMgrFocus != NULL)) {
    _InitTextEditSink(pDocMgrFocus);
  }

  if (!_InitKeyEventSink())
    goto ExitError;

  // Init failure is non-fatal: some apps don't provide DisplayAttributeInfo
  // (e.g. OpenGL stuff). The atom stays 0 and the attribute setter skips the
  // write, so no bogus atom reaches GUID_PROP_ATTRIBUTE.
  _InitDisplayAttributeGuidAtom();

  if (!_InitPreservedKey())
    goto ExitError;

  if (!_InitLanguageBar())
    goto ExitError;

  if (!_IsKeyboardOpen())
    _SetKeyboardOpen(TRUE);

  if (!_InitCompartment())
    goto ExitError;
  if (!_InitThreadFocusSink())
    goto ExitError;

  _EnsureServerConnected();

  return S_OK;

ExitError:
  Deactivate();
  return E_FAIL;
}

// 该注册表值仅 WeaselSetup（安装/设置）写入，变更极罕见；限频重读，
// 外部改动最迟 TTL 后的下一次焦点切换生效，与原逐次读取行为一致
static const ULONGLONG kToggleImeRefreshIntervalMs = 10 * 1000;

void WeaselTSF::_RefreshToggleImeOnOpenClose() {
  ULONGLONG now = GetTickCount64();
  if (_toggleImeReadTick != 0 &&
      now - _toggleImeReadTick < kToggleImeRefreshIntervalMs)
    return;
  _toggleImeReadTick = now;
  std::wstring value;
  RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                    L"ToggleImeOnOpenClose", value);
  _isToOpenClose = (value == L"yes");
}

STDMETHODIMP WeaselTSF::OnSetThreadFocus() {
  _RefreshToggleImeOnOpenClose();
  // Echo + ProcessKeyEvent(0) 两轮 IPC 为承重路径，保留（K21 考证）：
  // - Echo 是会话有效性闸门（服务端 FindSession）。若省掉而直接发
  //   ProcessKeyEvent(0)，会话已失效时服务端会对孤儿会话跑 process_key
  //   并把 m_active_session 指向死会话 id，改变服务端可见行为。
  // - ProcessKeyEvent(0)（keycode=0，librime 各 processor 均不处理）的
  //   作用是借 _Respond 拿回全量 status 快照：tray 菜单的中/英切换（作用于
  //   服务端 m_active_session）与 global_ascii_mode 的跨会话联动都可能
  //   在本客户端无通知的情况下改变本会话状态，焦点切换时须重新同步
  //   _status 与语言栏图标，省掉会让图标陈旧至下一次击键。
  if (m_client.Echo()) {
    m_client.ProcessKeyEvent(0);
    _ConsumeResponseIfFresh();
  }
  return S_OK;
}
STDMETHODIMP WeaselTSF::OnKillThreadFocus() {
  // Thread focus loss invalidates composition state, not already emitted
  // commit events. _AbortComposition advances the engine epoch and preserves
  // durable queued commits for their original document context.
  _AbortComposition();
  return S_OK;
}
BOOL WeaselTSF::_InitThreadFocusSink() {
  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return FALSE;
  if (FAILED(pSource->AdviseSink(IID_ITfThreadFocusSink,
                                 (ITfThreadFocusSink*)this,
                                 &_dwThreadFocusSinkCookie)))
    return FALSE;
  return TRUE;
}
void WeaselTSF::_UninitThreadFocusSink() {
  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return;
  if (FAILED(pSource->UnadviseSink(_dwThreadFocusSinkCookie)))
    return;
}

STDMETHODIMP WeaselTSF::OnActivated(REFCLSID clsid,
                                    REFGUID guidProfile,
                                    BOOL isActivated) {
  if (!IsEqualCLSID(clsid, c_clsidTextService)) {
    return S_OK;
  }

  if (isActivated) {
    _ShowLanguageBar(TRUE);
    _UpdateLanguageBar(_status);
  } else {
    _InvalidatePendingUI();
    _responseApplyQueue.Reset();
    _pendingCommit.clear();
    _DeleteCandidateList();
    _ShowLanguageBar(FALSE);
  }
  return S_OK;
}

void WeaselTSF::_Reconnect() {
  m_client.Disconnect();
  m_client.Connect();
  m_client.StartSession();
  _ConsumeResponseIfFresh();
}

namespace {
constexpr DWORD kServerExitObservationTimeoutMs = 250;
constexpr DWORD kServerLaunchQuickObservationTimeoutMs = 500;
constexpr DWORD kServerLaunchLeaseTimeoutMs = 2000;
constexpr DWORD kServerRecoveryPollMs = 25;
constexpr DWORD kServerPresentBackoffMs = 100;
constexpr DWORD kServerLaunchBusyBackoffMs = 100;
constexpr DWORD kServerQueueFailureBackoffMs = 100;
constexpr DWORD kServerLaunchFailureBaseBackoffMs = 250;
constexpr DWORD kServerLaunchFailureMaxBackoffMs = 2000;
constexpr DWORD kFirstKeyRecoveryPollMs = 5;

DWORD RemainingRecoveryTime(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  if (now >= deadline)
    return 0;
  return static_cast<DWORD>(deadline - now);
}

#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
enum class ServerRecoveryState : LONG {
  Idle = 0,
  Busy = 1,
  Backoff = 2,
};

volatile LONG g_serverRecoveryState =
    static_cast<LONG>(ServerRecoveryState::Idle);
alignas(8) volatile LONG64 g_serverRecoveryRetryAfter = 0;
volatile LONG g_serverRecoveryFailureStreak = 0;

ServerRecoveryState LoadServerRecoveryState() noexcept {
  return static_cast<ServerRecoveryState>(
      InterlockedCompareExchange(&g_serverRecoveryState, 0, 0));
}

ULONGLONG LoadServerRecoveryRetryAfter() noexcept {
  return static_cast<ULONGLONG>(
      InterlockedCompareExchange64(&g_serverRecoveryRetryAfter, 0, 0));
}

void EnterServerRecoveryBackoff(DWORD delay_ms) noexcept {
  InterlockedExchange64(
      &g_serverRecoveryRetryAfter,
      static_cast<LONG64>(GetTickCount64() + delay_ms));
  InterlockedExchange(&g_serverRecoveryState,
                      static_cast<LONG>(ServerRecoveryState::Backoff));
}

void ResetServerRecoveryFailureStreak() noexcept {
  InterlockedExchange(&g_serverRecoveryFailureStreak, 0);
}

void NoteServerRecoveryHealthy() noexcept {
  // A successful cold reconnect starts a fresh failure epoch. Do not disturb
  // an in-flight Busy worker; it still owns the right to publish its outcome.
  ResetServerRecoveryFailureStreak();
}

void EnterServerRecoveryFailureBackoff() noexcept {
  const LONG streak = InterlockedIncrement(&g_serverRecoveryFailureStreak);
  const unsigned int shift =
      streak <= 1 ? 0U
                  : (streak >= 4 ? 3U
                                 : static_cast<unsigned int>(streak - 1));
  DWORD delay_ms = kServerLaunchFailureBaseBackoffMs << shift;
  if (delay_ms > kServerLaunchFailureMaxBackoffMs)
    delay_ms = kServerLaunchFailureMaxBackoffMs;
  EnterServerRecoveryBackoff(delay_ms);
}

bool TryBeginServerRecovery() noexcept {
  for (;;) {
    const ServerRecoveryState observed = LoadServerRecoveryState();
    if (observed == ServerRecoveryState::Busy)
      return false;

    if (observed == ServerRecoveryState::Backoff &&
        GetTickCount64() < LoadServerRecoveryRetryAfter()) {
      return false;
    }

    const LONG expected = static_cast<LONG>(observed);
    if (InterlockedCompareExchange(
            &g_serverRecoveryState,
            static_cast<LONG>(ServerRecoveryState::Busy),
            expected) == expected) {
      return true;
    }
  }
}

bool IsNamedMutexPresent(const std::wstring& mutex_name) noexcept {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name.c_str());
  if (mutex) {
    CloseHandle(mutex);
    return true;
  }

  // Be conservative if the object exists but its DACL denies SYNCHRONIZE:
  // treating it as absent could launch a duplicate server.
  return GetLastError() == ERROR_ACCESS_DENIED;
}

bool IsServerInstancePresent(
    const std::wstring& instance_name,
    const std::wstring& legacy_instance_name) noexcept {
  if (IsNamedMutexPresent(instance_name))
    return true;
  return legacy_instance_name != instance_name &&
         IsNamedMutexPresent(legacy_instance_name);
}

bool WaitForServerInstanceState(const std::wstring& instance_name,
                                const std::wstring& legacy_instance_name,
                                bool expected_present,
                                DWORD timeout_ms) noexcept {
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  for (;;) {
    if (IsServerInstancePresent(instance_name, legacy_instance_name) ==
        expected_present) {
      return true;
    }

    const DWORD remaining = RemainingRecoveryTime(deadline);
    if (!remaining) {
      return IsServerInstancePresent(instance_name, legacy_instance_name) ==
             expected_present;
    }

    Sleep(remaining < kServerRecoveryPollMs ? remaining
                                            : kServerRecoveryPollMs);
  }
}

struct LaunchMutexGuard {
  HANDLE handle = nullptr;
  bool owned = false;

  ~LaunchMutexGuard() {
    if (!handle)
      return;
    if (owned)
      ReleaseMutex(handle);
    CloseHandle(handle);
  }
};

void RunServerRecoveryOnce() noexcept {
  try {
    std::wstring dir;
    if (RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(),
                          L"WeaselRoot", dir) != ERROR_SUCCESS ||
        dir.empty()) {
      EnterServerRecoveryFailureBackoff();
      return;
    }

    const std::wstring instance_name = weasel::GetServerInstanceName();
    const std::wstring legacy_instance_name =
        weasel::GetLegacyServerInstanceName();
    const std::wstring launch_mutex_name = weasel::GetServerLaunchMutexName();

    // If an old server still owns the singleton object, allow only a short
    // background grace period for an already-in-progress exit. Never wait on
    // the TSF callback thread and never scan the process table.
    if (IsServerInstancePresent(instance_name, legacy_instance_name) &&
        !WaitForServerInstanceState(instance_name, legacy_instance_name, false,
                                    kServerExitObservationTimeoutMs)) {
      ResetServerRecoveryFailureStreak();
      EnterServerRecoveryBackoff(kServerPresentBackoffMs);
      return;
    }

    LaunchMutexGuard launch_mutex;
    launch_mutex.handle =
        CreateMutexW(nullptr, FALSE, launch_mutex_name.c_str());
    if (!launch_mutex.handle) {
      EnterServerRecoveryFailureBackoff();
      return;
    }

    const DWORD wait_result = WaitForSingleObject(launch_mutex.handle, 0);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
      ResetServerRecoveryFailureStreak();
      EnterServerRecoveryBackoff(kServerLaunchBusyBackoffMs);
      return;
    }
    launch_mutex.owned = true;

    // Cross-process double check: another TSF host may have launched the
    // service between our first observation and acquiring the launch mutex.
    if (IsServerInstancePresent(instance_name, legacy_instance_name)) {
      ResetServerRecoveryFailureStreak();
      EnterServerRecoveryBackoff(kServerPresentBackoffMs);
      return;
    }

    const HINSTANCE launched =
        ShellExecuteW(nullptr, L"open", (dir + L"\\start_service.bat").c_str(),
                      nullptr, dir.c_str(), SW_HIDE);
    if (reinterpret_cast<INT_PTR>(launched) <= 32) {
      EnterServerRecoveryFailureBackoff();
      return;
    }

    // Keep the cross-process launch lease until the launched server has had a
    // realistic chance to publish its singleton. The first 500 ms is the fast
    // observation window; if startup is cold/slow, retain the same launch
    // mutex for the rest of the 2 s lease so another TSF host cannot start a
    // competing WeaselServer and trigger an avoidable restart cycle.
    if (WaitForServerInstanceState(
            instance_name, legacy_instance_name, true,
            kServerLaunchQuickObservationTimeoutMs)) {
      ResetServerRecoveryFailureStreak();
      EnterServerRecoveryBackoff(kServerPresentBackoffMs);
      return;
    }

    static_assert(kServerLaunchLeaseTimeoutMs >=
                  kServerLaunchQuickObservationTimeoutMs);
    const DWORD remaining_launch_lease =
        kServerLaunchLeaseTimeoutMs - kServerLaunchQuickObservationTimeoutMs;
    if (remaining_launch_lease &&
        WaitForServerInstanceState(instance_name, legacy_instance_name, true,
                                   remaining_launch_lease)) {
      ResetServerRecoveryFailureStreak();
      EnterServerRecoveryBackoff(kServerPresentBackoffMs);
      return;
    }

    EnterServerRecoveryFailureBackoff();
  } catch (...) {
    EnterServerRecoveryFailureBackoff();
  }
}
#endif
}  // namespace

void WeaselTSF::_ScheduleServerRecovery() noexcept {
#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
  if (!TryBeginServerRecovery())
    return;

  bool queued = false;
  try {
    queued = QueueDllBackgroundTask([]() { RunServerRecoveryOnce(); });
  } catch (...) {
    // Scheduling is best-effort and must never escape through a TSF callback.
  }

  if (!queued)
    EnterServerRecoveryBackoff(kServerQueueFailureBackoffMs);
#endif
}

bool WeaselTSF::_EnsureServerConnectedUntil(ULONGLONG recovery_deadline) {
  if (m_client.IsSessionActive())
    return true;

  const auto reconnect_until = [&](ULONGLONG deadline) {
    m_client.Disconnect();

    DWORD remaining = RemainingRecoveryTime(deadline);
    if (!remaining || !m_client.Connect(remaining))
      return false;

    remaining = RemainingRecoveryTime(deadline);
    if (!remaining) {
      m_client.Disconnect();
      return false;
    }

    m_client.StartSession(remaining);
    _ConsumeResponseIfFresh();
    return m_client.IsSessionActive();
  };

  if (reconnect_until(recovery_deadline)) {
#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
    NoteServerRecoveryHealthy();
#endif
    _reconnectRetry = 0;
    return true;
  }

  _ScheduleServerRecovery();

  while (GetTickCount64() < recovery_deadline) {
    const DWORD remaining = RemainingRecoveryTime(recovery_deadline);
    if (!remaining)
      break;
    Sleep(remaining < kFirstKeyRecoveryPollMs ? remaining
                                              : kFirstKeyRecoveryPollMs);
    if (reconnect_until(recovery_deadline)) {
#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
      NoteServerRecoveryHealthy();
#endif
      _reconnectRetry = 0;
      return true;
    }
  }

  _reconnectRetry = 0;
  return false;
}

bool WeaselTSF::_EnsureServerConnected() {
  // The healthy input path is strictly local: no clocks, atomics, registry
  // access, process probing, or background scheduling while the session lives.
  if (m_client.IsSessionActive())
    return true;

  return _EnsureServerConnectedUntil(GetTickCount64() +
                                     kFastSessionRecoveryBudgetMs);
}
