#include "stdafx.h"

#include <WeaselIPCData.h>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "LanguageBar.h"
#include "Compartment.h"
#include "Globals.h"
#include "ServerRecoveryCoordinator.h"

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
      (pDocMgrFocus != NULL) && !_InitTextEditSink(pDocMgrFocus)) {
    goto ExitError;
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
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return FALSE;
  if (FAILED(pSource->AdviseSink(IID_ITfThreadFocusSink,
                                 (ITfThreadFocusSink*)this,
                                 &_dwThreadFocusSinkCookie))) {
    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
    return FALSE;
  }
  return TRUE;
}
void WeaselTSF::_UninitThreadFocusSink() {
  if (_dwThreadFocusSinkCookie == TF_INVALID_COOKIE)
    return;

  const DWORD cookie = _dwThreadFocusSinkCookie;
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

  com_ptr<ITfSource> pSource;
  if (!_pThreadMgr || FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return;
  pSource->UnadviseSink(cookie);
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
constexpr DWORD kServerLaunchObservationTimeoutMs = 2000;
constexpr DWORD kServerRecoveryPollMs = 25;
constexpr DWORD kServerPresentBackoffMs = 100;
constexpr DWORD kServerLifecycleBusyBackoffMs = 100;
constexpr DWORD kServerQueueFailureBackoffMs = 100;
constexpr DWORD kServerLaunchFailureBaseBackoffMs = 250;
constexpr DWORD kServerLaunchFailureMaxBackoffMs = 2000;
constexpr DWORD kServerUnavailableRestartThresholdMs = 1500;
constexpr DWORD kServerGracefulShutdownTimeoutMs = 1500;
constexpr DWORD kServerForceTerminateThresholdMs = 5000;
constexpr DWORD kServerForcedExitTimeoutMs = 1000;
constexpr DWORD kServerLivenessProbeTimeoutMs = 100;
constexpr DWORD kServerRecoveryWorkerRetryBudgetMs = 2500;
constexpr DWORD kFirstKeyRecoveryPollMs = 5;

DWORD RemainingRecoveryTime(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  if (now >= deadline)
    return 0;
  return static_cast<DWORD>(deadline - now);
}

#if !defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
weasel::tsf::ServerRecoveryCoordinator g_serverRecovery;

void ResetServerUnavailableObservation() noexcept {
  g_serverRecovery.ResetUnavailableObservation();
}
ULONGLONG ServerUnavailableDurationMs() noexcept {
  return g_serverRecovery.UnavailableDuration(GetTickCount64());
}
bool ObserveServerUnavailableLongEnough() noexcept {
  return g_serverRecovery.ObserveUnavailableLongEnough(
      GetTickCount64(), kServerUnavailableRestartThresholdMs);
}
void ResetServerRecoveryFailureStreak() noexcept {
  g_serverRecovery.ResetFailureStreak();
}
void FinishServerRecoveryBackoff(DWORD delay_ms) noexcept {
  g_serverRecovery.FinishBackoff(delay_ms, GetTickCount64());
}
void FinishServerRecoveryHealthy() noexcept {
  g_serverRecovery.FinishHealthy();
}
void NoteServerRecoveryHealthy() noexcept {
  g_serverRecovery.NoteHealthy();
}
void FinishServerRecoveryFailure() noexcept {
  g_serverRecovery.FinishFailure(GetTickCount64(),
                                 kServerLaunchFailureBaseBackoffMs,
                                 kServerLaunchFailureMaxBackoffMs);
}
bool TryBeginServerRecovery() noexcept {
  return g_serverRecovery.TryBegin(GetTickCount64());
}

bool IsNamedMutexPresent(const std::wstring& mutex_name) noexcept {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name.c_str());
  if (mutex) {
    CloseHandle(mutex);
    return true;
  }
  return GetLastError() == ERROR_ACCESS_DENIED;
}

bool ReadServerRuntimeState(weasel::ServerRuntimeState& state) noexcept {
  HANDLE mapping = OpenFileMappingW(
      FILE_MAP_READ, FALSE, weasel::GetServerRuntimeStateName().c_str());
  if (!mapping)
    return false;
  const auto* shared =
      static_cast<const weasel::ServerRuntimeState*>(MapViewOfFile(
          mapping, FILE_MAP_READ, 0, 0, sizeof(weasel::ServerRuntimeState)));
  if (!shared) {
    CloseHandle(mapping);
    return false;
  }
  state.version = shared->version;
  state.process_id = shared->process_id;
  state.lifecycle = *reinterpret_cast<const volatile LONG*>(&shared->lifecycle);
  state.reserved = shared->reserved;
  state.process_creation_time = shared->process_creation_time;
  UnmapViewOfFile(shared);
  CloseHandle(mapping);
  return state.version == weasel::kServerRuntimeStateVersion &&
         state.process_id != 0 && state.process_creation_time != 0;
}

bool SameServerGeneration(const weasel::ServerRuntimeState& lhs,
                          const weasel::ServerRuntimeState& rhs) noexcept {
  return lhs.version == rhs.version && lhs.process_id == rhs.process_id &&
         lhs.process_creation_time == rhs.process_creation_time;
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
        expected_present)
      return true;
    const DWORD remaining = RemainingRecoveryTime(deadline);
    if (!remaining)
      return IsServerInstancePresent(instance_name, legacy_instance_name) ==
             expected_present;
    Sleep(remaining < kServerRecoveryPollMs ? remaining
                                            : kServerRecoveryPollMs);
  }
}

struct RunningServerTarget {
  HWND window = nullptr;
  HANDLE process = nullptr;
  ~RunningServerTarget() {
    if (process)
      CloseHandle(process);
  }
};

HWND FindServerWindowForProcess(DWORD process_id) noexcept {
  HWND window = nullptr;
  while ((window = FindWindowExW(nullptr, window, nullptr,
                                 WEASEL_IPC_WINDOW)) != nullptr) {
    DWORD window_process_id = 0;
    if (GetWindowThreadProcessId(window, &window_process_id) &&
        window_process_id == process_id)
      return window;
  }
  return nullptr;
}

bool OpenCurrentSessionServerTarget(const weasel::ServerRuntimeState& state,
                                    RunningServerTarget& target) noexcept {
  target.process = OpenProcess(
      SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
      FALSE, state.process_id);
  if (!target.process)
    return false;
  DWORD current_session_id = 0, process_session_id = 0;
  if (!weasel::GetCurrentWindowsSessionId(current_session_id) ||
      !ProcessIdToSessionId(state.process_id, &process_session_id) ||
      process_session_id != current_session_id)
    return false;

  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(target.process, &created, &exited, &kernel, &user))
    return false;
  ULARGE_INTEGER creation{};
  creation.LowPart = created.dwLowDateTime;
  creation.HighPart = created.dwHighDateTime;
  if (creation.QuadPart != state.process_creation_time)
    return false;

  wchar_t image_path[32768] = {};
  DWORD image_path_length =
      static_cast<DWORD>(sizeof(image_path) / sizeof(image_path[0]));
  if (!QueryFullProcessImageNameW(target.process, 0, image_path,
                                  &image_path_length))
    return false;
  const std::wstring path(image_path, image_path_length);
  const size_t separator = path.find_last_of(L"\\/");
  const std::wstring file_name =
      separator == std::wstring::npos ? path : path.substr(separator + 1);
  if (_wcsicmp(file_name.c_str(), L"WeaselServer.exe") != 0)
    return false;
  target.window = FindServerWindowForProcess(state.process_id);
  return target.window != nullptr;
}

bool ProbeServerResponsive() noexcept {
  try {
    weasel::Client probe;
    const ULONGLONG deadline = GetTickCount64() + kServerLivenessProbeTimeoutMs;
    if (!probe.Connect(kServerLivenessProbeTimeoutMs))
      return false;
    const DWORD remaining = RemainingRecoveryTime(deadline);
    return remaining && probe.ProbeServer(remaining);
  } catch (...) {
    return false;
  }
}

enum class StopServerResult {
  Stopped,
  SupersededByHealth,
  Failed,
};

StopServerResult StopPersistentlyUnavailableServer(
    const weasel::ServerRuntimeState& expected_state,
    ULONGLONG health_revision) noexcept {
  RunningServerTarget target;
  if (!OpenCurrentSessionServerTarget(expected_state, target))
    return StopServerResult::Failed;

  weasel::ServerRuntimeState current_state{};
  if (!ReadServerRuntimeState(current_state) ||
      !SameServerGeneration(current_state, expected_state) ||
      current_state.lifecycle !=
          static_cast<LONG>(weasel::ServerLifecycle::Ready)) {
    return StopServerResult::Failed;
  }

  // Keep the potentially 100 ms IPC probe outside the coordinator lock.
  // ServerLifecycleLock is already owned by the worker, so no new START_SESSION
  // can commit while this final liveness observation is in flight.
  if (ProbeServerResponsive())
    return StopServerResult::SupersededByHealth;

  // Linearize only the irreversible graceful-shutdown decision against a
  // same-process foreground reconnect that completed before lifecycle
  // ownership was acquired. Cross-process START_SESSION is excluded by the
  // lifecycle lock above.
  const bool close_posted =
      g_serverRecovery.RunIfHealthUnchanged(health_revision, [&]() noexcept {
        weasel::ServerRuntimeState latest_state{};
        if (!ReadServerRuntimeState(latest_state) ||
            !SameServerGeneration(latest_state, expected_state) ||
            latest_state.lifecycle !=
                static_cast<LONG>(weasel::ServerLifecycle::Ready)) {
          return false;
        }
        return !!PostMessageW(target.window, WM_CLOSE, 0, 0);
      });
  if (!close_posted) {
    return g_serverRecovery.HealthRevision() != health_revision
               ? StopServerResult::SupersededByHealth
               : StopServerResult::Failed;
  }

  DWORD wait_result =
      WaitForSingleObject(target.process, kServerGracefulShutdownTimeoutMs);
  if (wait_result == WAIT_OBJECT_0)
    return StopServerResult::Stopped;
  if (wait_result == WAIT_FAILED)
    return StopServerResult::Failed;

  const ULONGLONG duration = ServerUnavailableDurationMs();
  if (duration < kServerForceTerminateThresholdMs) {
    const ULONGLONG remaining = kServerForceTerminateThresholdMs - duration;
    wait_result =
        WaitForSingleObject(target.process, static_cast<DWORD>(remaining));
    if (wait_result == WAIT_OBJECT_0)
      return StopServerResult::Stopped;
    if (wait_result == WAIT_FAILED)
      return StopServerResult::Failed;
  }

  // WM_CLOSE is the destructive-recovery commit point. Once it was posted,
  // complete the shutdown instead of letting a late health hint leave a
  // half-stopped generation alive without launching its replacement.
  if (!TerminateProcess(target.process, ERROR_TIMEOUT))
    return StopServerResult::Failed;
  return WaitForSingleObject(target.process, kServerForcedExitTimeoutMs) ==
                 WAIT_OBJECT_0
             ? StopServerResult::Stopped
             : StopServerResult::Failed;
}

void RunServerRecoveryOnce() noexcept {
  try {
    weasel::ServerLifecycleLock lifecycle_lock;
    if (!lifecycle_lock.Acquire(0)) {
      ResetServerRecoveryFailureStreak();
      ResetServerUnavailableObservation();
      FinishServerRecoveryBackoff(kServerLifecycleBusyBackoffMs);
      return;
    }

    const std::wstring instance_name = weasel::GetServerInstanceName();
    const std::wstring legacy_instance_name =
        weasel::GetLegacyServerInstanceName();
    const bool server_present =
        IsServerInstancePresent(instance_name, legacy_instance_name);

    const ULONGLONG health_revision = g_serverRecovery.HealthRevision();

    if (server_present) {
      weasel::ServerRuntimeState state{};
      if (!ReadServerRuntimeState(state) ||
          state.lifecycle !=
              static_cast<LONG>(weasel::ServerLifecycle::Ready)) {
        ResetServerRecoveryFailureStreak();
        ResetServerUnavailableObservation();
        FinishServerRecoveryBackoff(kServerPresentBackoffMs);
        return;
      }
      if (!ObserveServerUnavailableLongEnough()) {
        ResetServerRecoveryFailureStreak();
        FinishServerRecoveryBackoff(kServerPresentBackoffMs);
        return;
      }
      if (ProbeServerResponsive()) {
        FinishServerRecoveryHealthy();
        return;
      }

      weasel::ServerRuntimeState latest_state{};
      if (!ReadServerRuntimeState(latest_state) ||
          !SameServerGeneration(latest_state, state) ||
          latest_state.lifecycle !=
              static_cast<LONG>(weasel::ServerLifecycle::Ready)) {
        ResetServerRecoveryFailureStreak();
        ResetServerUnavailableObservation();
        FinishServerRecoveryBackoff(kServerPresentBackoffMs);
        return;
      }
      const StopServerResult stop_result =
          StopPersistentlyUnavailableServer(latest_state, health_revision);
      if (stop_result == StopServerResult::SupersededByHealth) {
        FinishServerRecoveryHealthy();
        return;
      }
      if (stop_result != StopServerResult::Stopped) {
        FinishServerRecoveryFailure();
        return;
      }
      ResetServerUnavailableObservation();
    } else {
      ResetServerUnavailableObservation();
    }

    std::wstring dir;
    if (RegGetStringValue(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY, L"WeaselRoot",
                          dir) != ERROR_SUCCESS ||
        dir.empty()) {
      FinishServerRecoveryFailure();
      return;
    }
    const HINSTANCE launched =
        ShellExecuteW(nullptr, L"open", (dir + L"\\start_service.bat").c_str(),
                      nullptr, dir.c_str(), SW_HIDE);
    if (reinterpret_cast<INT_PTR>(launched) <= 32) {
      FinishServerRecoveryFailure();
      return;
    }
    if (WaitForServerInstanceState(instance_name, legacy_instance_name, true,
                                   kServerLaunchObservationTimeoutMs)) {
      ResetServerRecoveryFailureStreak();
      ResetServerUnavailableObservation();
      FinishServerRecoveryBackoff(kServerPresentBackoffMs);
      return;
    }
    FinishServerRecoveryFailure();
  } catch (...) {
    FinishServerRecoveryFailure();
  }
}

void RunServerRecoveryWorker() noexcept {
  // One failed key must be enough to carry transient recovery through the
  // sustained-unavailability threshold even if the user types nothing else.
  // Keep the retry lease short: this callback is background-only and exits
  // once the bounded budget is exhausted.
  const ULONGLONG deadline =
      GetTickCount64() + kServerRecoveryWorkerRetryBudgetMs;

  for (;;) {
    RunServerRecoveryOnce();

    // Wait until the coordinator's current deadline, re-reading it after every
    // sleep because foreground health or another worker can clear/replace it.
    for (;;) {
      const ULONGLONG retry_after = g_serverRecovery.RetryAfter();
      if (!retry_after)
        return;

      const ULONGLONG now = GetTickCount64();
      if (now >= deadline || retry_after > deadline)
        return;

      if (now < retry_after) {
        Sleep(static_cast<DWORD>(retry_after - now));
        continue;
      }

      if (TryBeginServerRecovery())
        break;

      // Another callback may have won admission. If so it owns progress; if
      // not, loop and observe the deadline it just published.
      if (g_serverRecovery.WorkerInflight())
        return;
    }
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
    queued = QueueDllBackgroundTask([]() { RunServerRecoveryWorker(); });
  } catch (...) {
    // Scheduling is best-effort and must never escape through a TSF callback.
  }

  if (!queued)
    FinishServerRecoveryBackoff(kServerQueueFailureBackoffMs);
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
