#include "stdafx.h"
#include "WeaselServerImpl.h"
#include <PipeServer.h>
#include <mutex>
#include <vector>
#include <Windows.h>
#include <resource.h>
#include <WeaselUtility.h>

using namespace weasel;

extern CAppModule _Module;

// Serializes every RequestHandler call: pipe worker threads run under it
// in Run(); window-message paths (color change, tray menu, logoff) must
// take it too, or they race handler state (session map, rime) mid-call.
static std::mutex g_api_mutex;

// Bounded backoff before Listen retries a failed iteration: without it a
// persistently failing CreateNamedPipe (e.g. handle exhaustion) turns the
// loop into a 100% CPU spin. The sleep itself is interruptible, so shutdown
// (interrupt + WakeListener) is never delayed by the backoff.
static constexpr boost::chrono::milliseconds kListenRetryBackoff{50};

static constexpr DWORD kPrimaryPipeReadyTimeoutMs = 2000;
static constexpr DWORD kServerStartupLifecycleWaitMs = 2000;

class SynchronousPipeIoScope {
 public:
  explicit SynchronousPipeIoScope(std::atomic_bool& pending)
      : pending_(pending) {
    pending_.store(true, std::memory_order_release);
  }
  ~SynchronousPipeIoScope() {
    pending_.store(false, std::memory_order_release);
  }

 private:
  std::atomic_bool& pending_;
};

bool WaitForPrimaryPipeReady() noexcept {
  const std::wstring pipe_name = GetPipeName();
  const ULONGLONG deadline = GetTickCount64() + kPrimaryPipeReadyTimeoutMs;
  for (;;) {
    if (WaitNamedPipeW(pipe_name.c_str(), 50))
      return true;

    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY &&
        error != ERROR_SEM_TIMEOUT) {
      return false;
    }

    const ULONGLONG now = GetTickCount64();
    if (now >= deadline)
      return false;
    Sleep(10);
  }
}

ServerImpl::ServerImpl()
    : channel(std::make_unique<PipeServer>(GetPipeName(),
                                           sa.get_attr(),
                                           GetLegacyPipeName())),
      process_tracker(
          [this](ClientProcessToken token) { _OnClientProcessExit(token); }),
      m_pRequestHandler(NULL),
      m_runtimeStateMapping(NULL),
      m_runtimeState(nullptr),
      m_darkMode(IsUserDarkMode()) {
  m_hUser32Module = GetModuleHandle(_T("user32.dll"));
  m_pPhysicalToLogicalPointForPerMonitorDPI =
      reinterpret_cast<PPhysicalToLogicalPointForPerMonitorDPI>(
          ::GetProcAddress(m_hUser32Module,
                           "PhysicalToLogicalPointForPerMonitorDPI"));
}

ServerImpl::~ServerImpl() {
  _Finailize();
}

void ServerImpl::_Finailize() {
  _SetRuntimeLifecycle(ServerLifecycle::Stopping);
  m_startupLifecycleLock.Release();
  _StopPipeServer();
  process_tracker.Stop();

  if (IsWindow())
    DestroyWindow();
  if (m_runtimeState) {
    UnmapViewOfFile(m_runtimeState);
    m_runtimeState = nullptr;
  }
  if (m_runtimeStateMapping) {
    CloseHandle(m_runtimeStateMapping);
    m_runtimeStateMapping = NULL;
  }
}

void ServerImpl::_SetRuntimeLifecycle(ServerLifecycle lifecycle) noexcept {
  if (!m_runtimeState)
    return;

  // Publish the monotonic start tick before the lifecycle value. A reader that
  // observes the new lifecycle can therefore never pair it with an older
  // timestamp and prematurely classify the state as wedged.
  m_runtimeState->reserved = EncodeServerLifecycleTick(GetTickCount());
  InterlockedExchange(&m_runtimeState->lifecycle, static_cast<LONG>(lifecycle));
}

ServerLifecycle ServerImpl::_RuntimeLifecycle() const noexcept {
  if (!m_runtimeState)
    return ServerLifecycle::Stopping;
  const LONG lifecycle = InterlockedCompareExchange(
      const_cast<volatile LONG*>(&m_runtimeState->lifecycle), 0, 0);
  return static_cast<ServerLifecycle>(lifecycle);
}

bool ServerImpl::_TryTransitionRuntimeLifecycle(
    ServerLifecycle expected,
    ServerLifecycle desired) noexcept {
  if (!m_runtimeState)
    return false;

  // Stamping before the compare-exchange is conservative: a failed transition
  // can only make recovery wait longer for the still-current state, never make
  // it look older than it really is.
  m_runtimeState->reserved = EncodeServerLifecycleTick(GetTickCount());
  return InterlockedCompareExchange(
             &m_runtimeState->lifecycle, static_cast<LONG>(desired),
             static_cast<LONG>(expected)) == static_cast<LONG>(expected);
}

bool ServerImpl::_PublishRuntimeState() noexcept {
  m_runtimeStateMapping = CreateFileMappingW(
      INVALID_HANDLE_VALUE, sa.get_attr(), PAGE_READWRITE, 0,
      sizeof(ServerRuntimeState), GetServerRuntimeStateName().c_str());
  if (!m_runtimeStateMapping)
    return false;

  m_runtimeState = static_cast<ServerRuntimeState*>(MapViewOfFile(
      m_runtimeStateMapping, FILE_MAP_WRITE, 0, 0, sizeof(ServerRuntimeState)));
  if (!m_runtimeState)
    return false;

  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return false;

  ULARGE_INTEGER creation{};
  creation.LowPart = created.dwLowDateTime;
  creation.HighPart = created.dwHighDateTime;

  m_runtimeState->version = kServerRuntimeStateVersion;
  m_runtimeState->process_id = GetCurrentProcessId();
  m_runtimeState->reserved = 0;
  m_runtimeState->process_creation_time = creation.QuadPart;
  _SetRuntimeLifecycle(ServerLifecycle::Starting);
  return true;
}

void ServerImpl::_UnlockStartupLifecycleLock() noexcept {
  // Keep the handle open as the process-lifetime anchor for this named mutex;
  // only release ownership once startup reaches Ready.
  m_startupLifecycleLock.Unlock();
}

void ServerImpl::_OnClientProcessExit(ClientProcessToken token) {
  // ClientProcessTracker releases its metadata lock before invoking us. Enter
  // the same API serialization boundary as normal pipe dispatch so Rime and
  // SessionManager state can never be mutated concurrently.
  std::lock_guard guard(g_api_mutex);
  if (m_pRequestHandler)
    session_router.RemoveClient(token);

  if (!m_pRequestHandler || !m_maintenanceOwner ||
      *m_maintenanceOwner != token ||
      _RuntimeLifecycle() != ServerLifecycle::Maintenance) {
    return;
  }

  // The exact process generation that owned START_MAINTENANCE is gone. No
  // legitimate local librime operation can still be running there, so recover
  // immediately instead of leaving the server finalized indefinitely.
  LOG(WARNING) << "maintenance owner exited; recovering WeaselServer";
  m_maintenanceOwner.reset();
  try {
    m_pRequestHandler->EndMaintenance();
  } catch (...) {
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    PostMessage(WM_QUIT);
    return;
  }

  if (!m_pRequestHandler->IsReadyForSessions() ||
      !_TryTransitionRuntimeLifecycle(ServerLifecycle::Maintenance,
                                      ServerLifecycle::Ready)) {
    // Never publish a false Ready state. Process replacement is safer than
    // keeping a live IPC endpoint whose Rime backend cannot create sessions.
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    PostMessage(WM_QUIT);
  }
}

LRESULT ServerImpl::OnColorChange(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  if (IsUserDarkMode() != m_darkMode) {
    m_darkMode = IsUserDarkMode();
    if (m_pRequestHandler) {
      std::lock_guard guard(g_api_mutex);
      m_pRequestHandler->UpdateColorTheme(m_darkMode);
    }
  }
  return 0;
}

LRESULT ServerImpl::OnCreate(UINT uMsg,
                             WPARAM wParam,
                             LPARAM lParam,
                             BOOL& bHandled) {
  // not neccessary...
  ::SetWindowText(m_hWnd, WEASEL_IPC_WINDOW);
  return 0;
}

LRESULT ServerImpl::OnClose(UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL& bHandled) {
  Stop();
  return 0;
}

LRESULT ServerImpl::OnDestroy(UINT uMsg,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL& bHandled) {
  bHandled = FALSE;
  return 1;
}

LRESULT ServerImpl::OnQueryEndSystemSession(UINT uMsg,
                                            WPARAM wParam,
                                            LPARAM lParam,
                                            BOOL& bHandled) {
  return TRUE;
}

LRESULT ServerImpl::OnEndSystemSession(UINT uMsg,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL& bHandled) {
  std::lock_guard guard(g_api_mutex);
  _SetRuntimeLifecycle(ServerLifecycle::Stopping);
  if (m_pRequestHandler) {
    m_pRequestHandler->Finalize();
    session_router.SetBackend(nullptr);
    m_pRequestHandler = nullptr;
  }
  return 0;
}

LRESULT ServerImpl::OnCommand(UINT uMsg,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL& bHandled) {
  UINT uID = LOWORD(wParam);
  // Window commands do not run under Run()'s pipe dispatch lock. Only
  // option changes touch RequestHandler; keep shell/menu callbacks outside
  // the lock, as before.
  if (uID == ID_WEASELTRAY_ENABLE_ASCII || uID == ID_WEASELTRAY_DISABLE_ASCII) {
    std::lock_guard guard(g_api_mutex);
    return _HandleCommand(uID, lParam, bHandled);
  }
  return _HandleCommand(uID, lParam, bHandled);
}

LRESULT ServerImpl::_HandleCommand(UINT uID, LPARAM lParam, BOOL& bHandled) {
  switch (uID) {
    case ID_WEASELTRAY_ENABLE_ASCII:
      if (m_pRequestHandler) {
        m_pRequestHandler->SetOption(lParam, "ascii_mode", true);
      }
      return 0;
    case ID_WEASELTRAY_DISABLE_ASCII:
      if (m_pRequestHandler) {
        m_pRequestHandler->SetOption(lParam, "ascii_mode", false);
      }
      return 0;
    default:;
  }

  std::map<UINT, CommandHandler>::iterator it = m_MenuHandlers.find(uID);
  if (it == m_MenuHandlers.end()) {
    bHandled = FALSE;
    return 0;
  }
  it->second();  // execute command
  return 0;
}

LRESULT ServerImpl::OnServiceNotifyMessage(UINT uMsg,
                                           WPARAM wParam,
                                           LPARAM lParam,
                                           BOOL& bHandled) {
  // Runs on the server message thread, NOT on a pipe worker thread and
  // without holding g_api_mutex, so that Shell_NotifyIcon inside the tray
  // update can never deadlock against the taskbar UI thread.
  if (m_trayRefreshCallback) {
    m_trayRefreshCallback();
  }
  return 0;
}

DWORD ServerImpl::OnCommand(WEASEL_IPC_COMMAND uMsg,
                            DWORD wParam,
                            DWORD lParam) {
  const UINT command = LOWORD(wParam);
  // Run() already holds g_api_mutex for every pipe command. The quit menu
  // handler calls Stop(), which would try to acquire the same non-recursive
  // mutex again on this worker thread. Use the lock-held stop helper directly.
  if (command == ID_WEASELTRAY_QUIT) {
    _RequestStopUnderApiLock();
    return 1;
  }

  BOOL handled = TRUE;
  _HandleCommand(command, lParam, handled);
  return handled;
}

HWND ServerImpl::Start() {
  const std::wstring instanceName = GetServerInstanceName();
  HANDLE hMutexOneInstance = ::CreateMutex(NULL, FALSE, instanceName.c_str());
  const DWORD mutexError = ::GetLastError();
  if (!hMutexOneInstance) {
    LOG(ERROR) << "CreateMutex for the WeaselServer singleton failed, error: "
               << mutexError;
    return 0;
  }
  if (mutexError == ERROR_ALREADY_EXISTS) {
    ::CloseHandle(hMutexOneInstance);
    LOG(ERROR) << "another WeaselServer instance is already running";
    return 0;
  }

  // Publish the singleton before waiting for lifecycle ownership. Recovery can
  // observe the launched child without letting initialization overlap deploy.
  if (!m_startupLifecycleLock.Acquire(kServerStartupLifecycleWaitMs,
                                      sa.get_attr())) {
    LOG(WARNING) << "Timed out waiting for WeaselServer lifecycle ownership; "
                    "another deploy/recovery operation is still active.";
    CloseHandle(hMutexOneInstance);
    return 0;
  }

  if (!_PublishRuntimeState()) {
    LOG(ERROR) << "Failed to publish WeaselServer runtime state, error: "
               << GetLastError();
    if (m_runtimeState) {
      UnmapViewOfFile(m_runtimeState);
      m_runtimeState = nullptr;
    }
    if (m_runtimeStateMapping) {
      CloseHandle(m_runtimeStateMapping);
      m_runtimeStateMapping = NULL;
    }
    m_startupLifecycleLock.Release();
    CloseHandle(hMutexOneInstance);
    return 0;
  }

  HWND hwnd = Create(NULL);
  if (!hwnd) {
    LOG(ERROR) << "ServerImpl::Create failed, error: " << GetLastError();
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    UnmapViewOfFile(m_runtimeState);
    m_runtimeState = nullptr;
    CloseHandle(m_runtimeStateMapping);
    m_runtimeStateMapping = NULL;
    m_startupLifecycleLock.Release();
    CloseHandle(hMutexOneInstance);
  }
  return hwnd;
}

int ServerImpl::_RequestStopUnderApiLock() noexcept {
  _SetRuntimeLifecycle(ServerLifecycle::Stopping);
  // DO NOT exit process or finalize here. Let WeaselServer unwind Run().
  PostMessage(WM_QUIT);
  return 0;
}

int ServerImpl::Stop() {
  // Lifecycle publication must linearize with pipe handlers that can enter or
  // leave maintenance. OnShutdownServer already runs under this lock and calls
  // the lock-held helper directly.
  std::lock_guard guard(g_api_mutex);
  return _RequestStopUnderApiLock();
}

int ServerImpl::Run() {
  if (!m_pRequestHandler || !m_pRequestHandler->IsReadyForSessions()) {
    LOG(ERROR) << "Rime backend is not ready for input sessions";
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    return -1;
  }

  // This workaround causes a VC internal error:
  // void PipeServer::Listen(ServerHandler handler);
  //
  // auto handler = boost::bind(&ServerImpl::HandlePipeMessage, this);
  // auto listener = boost::bind(&PipeServer::Listen, channel.get(), handler);
  //
  auto listener = [this](const PipeServer::ConnectionContext& context,
                         PipeMessage msg, PipeServer::Respond resp) -> void {
    const bool starting = msg.Msg == WEASEL_IPC_START_SESSION;
    const bool maintenance_command = msg.Msg == WEASEL_IPC_START_MAINTENANCE ||
                                     msg.Msg == WEASEL_IPC_END_MAINTENANCE;
    const bool needs_process_owner = starting || maintenance_command;

    // Resolve the peer process generation before taking either lifecycle or
    // API ownership. Resolve() may open a process handle or synchronously
    // drain an old PID generation. START_SESSION stays compatible with legacy
    // peers whose PID cannot be resolved; maintenance never does, because an
    // untracked owner could leave the server permanently finalized.
    std::optional<ClientProcessToken> owner;
    if (needs_process_owner && context.client_pid)
      owner = process_tracker.Resolve(*context.client_pid);

    // All paths that need both locks use the same order:
    // ServerLifecycleLock -> g_api_mutex. Recovery/deployer already own the
    // lifecycle lock before probing or sending maintenance IPC, so taking the
    // locks in the opposite order here would create an AB/BA deadlock.
    ServerLifecycleLock session_lifecycle;
    const bool lifecycle_admitted = !starting || session_lifecycle.Acquire(0);

    // Defer the response write until the api lock is released: a slow or
    // hung client must not extend the global critical section.
    PipeServer::Msg result = 0;
    {
      std::lock_guard guard(g_api_mutex);
      bool dispatch = lifecycle_admitted;
      if (starting && dispatch) {
        // Lifecycle may have reached Stopping before this request acquired the
        // API lock. Recheck under serialization before creating any session.
        if (_RuntimeLifecycle() != ServerLifecycle::Ready ||
            (owner && !process_tracker.IsAlive(*owner))) {
          dispatch = false;
        } else {
          session_router.SetCurrentOwner(SessionOwner{owner});
        }
      }

      if (maintenance_command && dispatch) {
        if (!owner || !process_tracker.IsAlive(*owner)) {
          dispatch = false;
        } else {
          m_dispatchProcessOwner = owner;
        }
      }

      if (dispatch) {
        try {
          HandlePipeMessage(msg, [&result](PipeServer::Msg r) { result = r; });
        } catch (...) {
          if (starting)
            session_router.ClearCurrentOwner();
          m_dispatchProcessOwner.reset();
          throw;
        }
      }
      if (starting)
        session_router.ClearCurrentOwner();
      m_dispatchProcessOwner.reset();
    }

    if (starting && lifecycle_admitted) {
      // Lifecycle ownership only linearizes START_SESSION admission and the
      // committed session state under g_api_mutex. The synchronous response
      // write is transport work and may block on a client that is not reading;
      // never extend the cross-process lifecycle critical section across it.
      session_lifecycle.Unlock();
    }
    resp(result);
  };
  // Capture the listener by value: a stack reference would dangle as soon
  // as Run() returns while the pipe thread is still draining requests.
  pipeThread = std::make_unique<boost::thread>(
      [this, listener]() { channel->Listen(listener); });

  // WeaselServerApp calls Run() only after Rime initialization has completed.
  // Do not expose Ready until the primary pipe instance actually exists:
  // deployers interpret a failed Connect while owning the lifecycle lock as
  // "no live server" and may otherwise start local Rime maintenance beside us.
  if (!WaitForPrimaryPipeReady()) {
    LOG(ERROR) << "primary IPC pipe did not become ready during server startup";
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    // Startup lifecycle ownership stays held through WeaselServerApp cleanup;
    // ServerImpl destruction releases it after Rime/UI teardown.
    _StopPipeServer();
    return -1;
  }

  if (!_TryTransitionRuntimeLifecycle(ServerLifecycle::Starting,
                                      ServerLifecycle::Ready)) {
    _StopPipeServer();
    return -1;
  }
  _UnlockStartupLifecycleLock();

  CMessageLoop theLoop;
  _Module.AddMessageLoop(&theLoop);
  int nRet = theLoop.Run();
  _Module.RemoveMessageLoop();
  {
    // A maintenance handler that linearized first may finish cleanly; one that
    // arrives after this point observes Stopping and is rejected.
    std::lock_guard guard(g_api_mutex);
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
  }
  // Stop the pipe server before the caller (WeaselServerApp::Run) starts
  // destroying the handler and UI: late requests would touch freed objects.
  _StopPipeServer();
  // Process-exit waits are another asynchronous source that can touch the
  // handler. Drain them at the same lifetime boundary before returning.
  process_tracker.Stop();
  return nRet;
}

void ServerImpl::_StopPipeServer() {
  if (!pipeThread)
    return;
  channel->StopListener(*pipeThread);
  channel->DrainWorkers();
  pipeThread.reset();
}

DWORD ServerImpl::OnEcho(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam) {
  if (!m_pRequestHandler)
    return 0;
  return m_pRequestHandler->FindSession(lParam);
}

DWORD ServerImpl::OnStartSession(WEASEL_IPC_COMMAND uMsg,
                                 DWORD wParam,
                                 DWORD lParam) {
  // START_SESSION lifecycle admission is acquired by the pipe listener before
  // g_api_mutex. Keep this handler side-effect-free when lifecycle is not
  // Ready.
  if (!m_pRequestHandler || _RuntimeLifecycle() != ServerLifecycle::Ready)
    return 0;

  return m_pRequestHandler->AddSession(
      reinterpret_cast<LPWSTR>(channel->ReceiveBuffer()),
      [this](std::wstring& msg) -> bool {
        *channel << msg;
        return true;
      });
}

DWORD ServerImpl::OnEndSession(WEASEL_IPC_COMMAND uMsg,
                               DWORD wParam,
                               DWORD lParam) {
  if (!m_pRequestHandler)
    return 0;
  return m_pRequestHandler->RemoveSession(lParam);
}

DWORD ServerImpl::OnKeyEvent(WEASEL_IPC_COMMAND uMsg,
                             DWORD wParam,
                             DWORD lParam) {
  if (!m_pRequestHandler /* || !m_pSharedMemory*/)
    return 0;

  auto eat = [this](std::wstring& msg) -> bool {
    *channel << msg;
    return true;
  };
  return m_pRequestHandler->ProcessKeyEvent(KeyEvent(wParam), lParam, eat);
}

DWORD ServerImpl::OnShutdownServer(WEASEL_IPC_COMMAND uMsg,
                                   DWORD wParam,
                                   DWORD lParam) {
  // Pipe dispatch already owns g_api_mutex here.
  _RequestStopUnderApiLock();
  return 0;
}

DWORD ServerImpl::OnFocusIn(WEASEL_IPC_COMMAND uMsg,
                            DWORD wParam,
                            DWORD lParam) {
  if (!m_pRequestHandler)
    return 0;
  m_pRequestHandler->FocusIn(wParam, lParam);
  return 0;
}

DWORD ServerImpl::OnFocusOut(WEASEL_IPC_COMMAND uMsg,
                             DWORD wParam,
                             DWORD lParam) {
  if (!m_pRequestHandler)
    return 0;
  m_pRequestHandler->FocusOut(wParam, lParam);
  return 0;
}

DWORD ServerImpl::OnUpdateInputPosition(WEASEL_IPC_COMMAND uMsg,
                                        DWORD wParam,
                                        DWORD lParam) {
  if (!m_pRequestHandler)
    return 0;
  /*
   * 移位标志 = 1bit == 0
   * height: 0~127 = 7bit
   * top:-2048~2047 = 12bit（有符号）
   * left:-2048~2047 = 12bit（有符号）
   *
   * 高解析度下：
   * 移位标志 = 1bit == 1
   * height: 0~254 = 7bit（舍弃低1位）
   * top: -4096~4094 = 12bit（有符号，舍弃低1位）
   * left: -4096~4094 = 12bit（有符号，舍弃低1位）
   */
  RECT rc;
  int hi_res = (wParam >> 31) & 0x01;
  rc.left = ((wParam & 0x7ff) - (wParam & 0x800)) << hi_res;
  rc.top = (((wParam >> 12) & 0x7ff) - ((wParam >> 12) & 0x800)) << hi_res;
  const int width = 6;
  int height = ((wParam >> 24) & 0x7f) << hi_res;
  rc.right = rc.left + width;
  rc.bottom = rc.top + height;

  // PhysicalToLogicalPointForPerMonitorDPI exists since Win8.1 (the process
  // gate in WeaselServer.cpp enforces it); if the lookup still failed, skip
  // the conversion instead of calling a null pointer.
  if (m_pPhysicalToLogicalPointForPerMonitorDPI) {
    POINT lt = {rc.left, rc.top};
    POINT rb = {rc.right, rc.bottom};
    m_pPhysicalToLogicalPointForPerMonitorDPI(NULL, &lt);
    m_pPhysicalToLogicalPointForPerMonitorDPI(NULL, &rb);
    rc = {lt.x, lt.y, rb.x, rb.y};
  }

  m_pRequestHandler->UpdateInputPosition(rc, lParam);
  return 0;
}

DWORD ServerImpl::OnStartMaintenance(WEASEL_IPC_COMMAND uMsg,
                                     DWORD wParam,
                                     DWORD lParam) {
  if (!m_pRequestHandler || !m_pRequestHandler->IsReadyForSessions() ||
      !m_dispatchProcessOwner ||
      !_TryTransitionRuntimeLifecycle(ServerLifecycle::Ready,
                                      ServerLifecycle::Maintenance)) {
    return 0;
  }

  // Publish the owner before finalizing Rime. If it exits immediately, the
  // process tracker callback queues behind g_api_mutex and recovers this exact
  // maintenance generation after the acknowledgement path releases the lock.
  m_maintenanceOwner = m_dispatchProcessOwner;
  try {
    m_pRequestHandler->StartMaintenance();
  } catch (...) {
    m_maintenanceOwner.reset();
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    PostMessage(WM_QUIT);
    throw;
  }

  // Run() holds g_api_mutex around this call, so the acknowledgement is sent
  // only after every earlier handler call has finished and maintenance entry
  // has completed.
  return 1;
}

DWORD ServerImpl::OnEndMaintenance(WEASEL_IPC_COMMAND uMsg,
                                   DWORD wParam,
                                   DWORD lParam) {
  if (!m_pRequestHandler || !m_dispatchProcessOwner || !m_maintenanceOwner ||
      *m_dispatchProcessOwner != *m_maintenanceOwner ||
      _RuntimeLifecycle() != ServerLifecycle::Maintenance) {
    return 0;
  }

  // Only the process generation that successfully entered Maintenance may
  // resume it. A second deployer cannot release another process's operation.
  try {
    m_pRequestHandler->EndMaintenance();
  } catch (...) {
    m_maintenanceOwner.reset();
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    PostMessage(WM_QUIT);
    throw;
  }
  m_maintenanceOwner.reset();

  if (!m_pRequestHandler->IsReadyForSessions()) {
    _SetRuntimeLifecycle(ServerLifecycle::Stopping);
    PostMessage(WM_QUIT);
    return 0;
  }

  // Stop() may have published Stopping while EndMaintenance was running.
  // Never let a late maintenance reply move the lifecycle backwards to Ready.
  return _TryTransitionRuntimeLifecycle(ServerLifecycle::Maintenance,
                                        ServerLifecycle::Ready)
             ? 1
             : 0;
}

DWORD ServerImpl::OnCommitComposition(WEASEL_IPC_COMMAND uMsg,
                                      DWORD wParam,
                                      DWORD lParam) {
  if (m_pRequestHandler) {
    auto eat = [this](std::wstring& msg) -> bool {
      *channel << msg;
      return true;
    };
    m_pRequestHandler->CommitComposition(lParam, eat);
  }
  return 0;
}

DWORD ServerImpl::OnClearComposition(WEASEL_IPC_COMMAND uMsg,
                                     DWORD wParam,
                                     DWORD lParam) {
  if (m_pRequestHandler)
    m_pRequestHandler->ClearComposition(lParam);
  return 0;
}

DWORD ServerImpl::OnSelectCandidateOnCurrentPage(WEASEL_IPC_COMMAND uMsg,
                                                 DWORD wParam,
                                                 DWORD lParam) {
  if (m_pRequestHandler) {
    auto eat = [this](std::wstring& msg) -> bool {
      *channel << msg;
      return true;
    };
    m_pRequestHandler->SelectCandidateOnCurrentPage(wParam, lParam, eat);
  }
  return 0;
}

DWORD ServerImpl::OnHighlightCandidateOnCurrentPage(WEASEL_IPC_COMMAND uMsg,
                                                    DWORD wParam,
                                                    DWORD lParam) {
  if (m_pRequestHandler) {
    auto eat = [this](std::wstring& msg) -> bool {
      *channel << msg;
      return true;
    };
    m_pRequestHandler->HighlightCandidateOnCurrentPage(wParam, lParam, eat);
  }
  return 0;
}

DWORD ServerImpl::OnChangePage(WEASEL_IPC_COMMAND uMsg,
                               DWORD wParam,
                               DWORD lParam) {
  if (m_pRequestHandler) {
    auto eat = [this](std::wstring& msg) -> bool {
      *channel << msg;
      return true;
    };
    m_pRequestHandler->ChangePage(wParam, lParam, eat);
  }
  return 0;
}

#define MAP_PIPE_MSG_HANDLE(__msg, __wParam, __lParam) \
  {                                                    \
    auto lParam = __lParam;                            \
    auto wParam = __wParam;                            \
    LRESULT _result = 0;                               \
    switch (__msg) {
#define PIPE_MSG_HANDLE(__msg, __func)       \
  case __msg:                                \
    _result = __func(__msg, wParam, lParam); \
    break;

#define END_MAP_PIPE_MSG_HANDLE(__result) \
  }                                       \
  __result = _result;                     \
  }
template <typename _Resp>
void ServerImpl::HandlePipeMessage(PipeMessage pipe_msg, _Resp resp) {
  DWORD result;

  MAP_PIPE_MSG_HANDLE(pipe_msg.Msg, pipe_msg.wParam, pipe_msg.lParam)
  PIPE_MSG_HANDLE(WEASEL_IPC_ECHO, OnEcho)
  PIPE_MSG_HANDLE(WEASEL_IPC_START_SESSION, OnStartSession)
  PIPE_MSG_HANDLE(WEASEL_IPC_END_SESSION, OnEndSession)
  PIPE_MSG_HANDLE(WEASEL_IPC_PROCESS_KEY_EVENT, OnKeyEvent)
  PIPE_MSG_HANDLE(WEASEL_IPC_SHUTDOWN_SERVER, OnShutdownServer)
  PIPE_MSG_HANDLE(WEASEL_IPC_FOCUS_IN, OnFocusIn)
  PIPE_MSG_HANDLE(WEASEL_IPC_FOCUS_OUT, OnFocusOut)
  PIPE_MSG_HANDLE(WEASEL_IPC_UPDATE_INPUT_POS, OnUpdateInputPosition)
  PIPE_MSG_HANDLE(WEASEL_IPC_START_MAINTENANCE, OnStartMaintenance)
  PIPE_MSG_HANDLE(WEASEL_IPC_END_MAINTENANCE, OnEndMaintenance)
  PIPE_MSG_HANDLE(WEASEL_IPC_COMMIT_COMPOSITION, OnCommitComposition)
  PIPE_MSG_HANDLE(WEASEL_IPC_CLEAR_COMPOSITION, OnClearComposition);
  PIPE_MSG_HANDLE(WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE,
                  OnSelectCandidateOnCurrentPage);
  PIPE_MSG_HANDLE(WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE,
                  OnHighlightCandidateOnCurrentPage);
  PIPE_MSG_HANDLE(WEASEL_IPC_CHANGE_PAGE, OnChangePage);
  PIPE_MSG_HANDLE(WEASEL_IPC_TRAY_COMMAND, OnCommand);
  END_MAP_PIPE_MSG_HANDLE(result);

  resp(result);
}

PipeServer::PipeServer(std::wstring&& pn_cmd,
                       SECURITY_ATTRIBUTES* s,
                       std::wstring compatibility_alias)
    : PipeChannel(std::move(pn_cmd),
                  s,
                  64 * 1024,
                  std::move(compatibility_alias)) {
  m_reaper_thread =
      std::make_shared<boost::thread>([this] { _WorkerReaperLoop(); });
}

PipeServer::~PipeServer() {
  // Normal ServerImpl teardown already stops both listeners and drains workers.
  // Keep the alias cleanup idempotent for direct/test users of PipeServer.
  if (m_alias_listener && m_alias_listener->joinable()) {
    _StopListenerThread(*m_alias_listener, fallback_pname);
  }
  m_alias_listener.reset();
  DrainWorkers();
  {
    std::lock_guard<std::mutex> lock(m_workers_mutex);
    m_reaper_stop = true;
  }
  m_workers_cv.notify_all();
  if (m_reaper_thread && m_reaper_thread->joinable())
    m_reaper_thread->join();
}

void PipeServer::Listen(ServerHandler const& handler) {
  // Keep the old per-user endpoint alive for already-loaded TSF DLLs during an
  // upgrade. In another Windows session the legacy endpoint belongs to that
  // session's logon SID and CreateNamedPipe will be denied; the alias listener
  // exits while the session-scoped primary listener remains healthy.
  if (!fallback_pname.empty() && fallback_pname != pname) {
    m_alias_listener = std::make_shared<boost::thread>(
        [this, handler] { _ListenOn(fallback_pname, handler, true); });
  }
  _ListenOn(pname, handler, false);
}

void PipeServer::_ListenOn(std::wstring& pipe_name,
                           ServerHandler const& handler,
                           bool compatibility_alias) {
  for (;;) {
    boost::this_thread::interruption_point();
    HANDLE pipe = INVALID_HANDLE_VALUE;
    try {
      pipe = _ConnectServerPipe(pipe_name);
      _LaunchWorker(pipe, handler);
    } catch (DWORD error) {
      FinalizePipeHandle(pipe);
      if (compatibility_alias && error == ERROR_ACCESS_DENIED)
        return;
      boost::this_thread::sleep_for(kListenRetryBackoff);
    } catch (...) {  // thread spawn failures and non-Win32 exceptions alike
      FinalizePipeHandle(pipe);
      boost::this_thread::sleep_for(kListenRetryBackoff);
    }
    boost::this_thread::interruption_point();
  }
}

void PipeServer::_WakeListener(const std::wstring& pipe_name) {
  if (pipe_name.empty())
    return;
  // A client-side connection to our own pipe makes ConnectNamedPipe return
  // so an interrupted listener can reach its interruption point and exit.
  HANDLE wake = ::CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
  if (wake != INVALID_HANDLE_VALUE)
    ::CloseHandle(wake);
}

void PipeServer::WakeListener() {
  _WakeListener(pname);
  if (fallback_pname != pname)
    _WakeListener(fallback_pname);
}

void PipeServer::_StopListenerThread(boost::thread& listener,
                                     const std::wstring& pipe_name) {
  listener.interrupt();

  // interrupt() is cooperative and cannot itself cancel a synchronous
  // ConnectNamedPipe(). A single self-connect is also racy: it can happen
  // before the listener creates its next pipe instance. Retry until the
  // listener actually exits, cancelling whichever synchronous call is
  // current on each pass.
  while (listener.joinable()) {
    _WakeListener(pipe_name);
    const auto native_thread = listener.native_handle();
    if (native_thread)
      ::CancelSynchronousIo(native_thread);

    if (listener.timed_join(boost::posix_time::milliseconds(50)))
      break;
  }
}

void PipeServer::StopListener(boost::thread& listener) {
  // Join the primary first. Listen() creates the alias before entering the
  // primary accept loop, so after this join m_alias_listener can no longer
  // race from null to live behind shutdown's back.
  _StopListenerThread(listener, pname);
  if (m_alias_listener && m_alias_listener->joinable()) {
    _StopListenerThread(*m_alias_listener, fallback_pname);
  }
  m_alias_listener.reset();
}

void PipeServer::DrainWorkers() {
  std::vector<std::shared_ptr<boost::thread>> workers;
  {
    std::lock_guard<std::mutex> lock(m_workers_mutex);
    for (auto& pair : m_workers) {
      auto& entry = pair.second;
      if (!entry.finished) {
        // CancelSynchronousIo is thread-wide, not handle-specific. Only use
        // it while this worker explicitly marks that it is inside the
        // synchronous pipe ReadFile/WriteFile window; never cancel unrelated
        // synchronous I/O that a request handler (for example librime) may be
        // performing on the same worker thread.
        if (entry.pipe_io_pending &&
            entry.pipe_io_pending->load(std::memory_order_acquire) &&
            entry.thread && entry.thread->joinable()) {
          const auto native_thread = entry.thread->native_handle();
          if (native_thread)
            ::CancelSynchronousIo(native_thread);
        }
        ::CancelIoEx(entry.pipe, NULL);
        ::DisconnectNamedPipe(entry.pipe);
      }
      workers.push_back(entry.thread);
    }
    m_workers.clear();
  }
  for (auto& worker : workers)
    if (worker->joinable())
      worker->join();
}

void PipeServer::_LaunchWorker(HANDLE pipe, ServerHandler const& handler) {
  // The lock must cover thread creation AND registration: a worker whose
  // client dropped instantly may finish the moment it starts. Its finish
  // marker must queue behind registration so ownership cannot be lost.
  std::lock_guard<std::mutex> lock(m_workers_mutex);
  WorkerId worker_id = 0;
  do {
    ++m_next_worker_id;
    if (m_next_worker_id == 0)
      ++m_next_worker_id;
    worker_id = m_next_worker_id;
  } while (m_workers.find(worker_id) != m_workers.end());

  // Allocate and register the ownership slot before starting the thread.
  // If map allocation fails, no worker exists yet and Listen() may safely
  // close the pipe. Once the thread exists, its slot is guaranteed to exist;
  // because m_workers_mutex is still held, an immediately finishing worker
  // can only enter _MarkWorkerFinished() after thread ownership is published.
  auto pipe_io_pending = std::make_shared<std::atomic_bool>(false);
  auto [it, inserted] = m_workers.emplace(
      worker_id, WorkerEntry{pipe, nullptr, pipe_io_pending, false});
  if (!inserted)
    throw static_cast<DWORD>(ERROR_ALREADY_EXISTS);

  try {
    // Copy the handler into the worker: it must not reference anything
    // on this thread's stack.
    auto worker = std::make_shared<boost::thread>(
        [this, worker_id, pipe, pipe_io_pending, handler] {
          _ProcessPipeThread(worker_id, pipe, pipe_io_pending, handler);
        });
    it->second.thread = std::move(worker);
  } catch (...) {
    // Thread construction failed before any worker could run. Roll back the
    // pre-registered slot so Listen() remains the sole owner of |pipe|.
    m_workers.erase(it);
    throw;
  }
}

void PipeServer::_MarkWorkerFinished(WorkerId worker_id) {
  {
    std::lock_guard<std::mutex> lock(m_workers_mutex);
    auto it = m_workers.find(worker_id);
    if (it != m_workers.end()) {
      it->second.finished = true;
    } else {
      // DrainWorkers may already own this thread and be joining it.
      return;
    }
  }
  m_workers_cv.notify_one();
}

void PipeServer::_WorkerReaperLoop() {
  for (;;) {
    std::vector<std::shared_ptr<boost::thread>> finished;
    {
      std::unique_lock<std::mutex> lock(m_workers_mutex);
      m_workers_cv.wait(lock, [this] {
        if (m_reaper_stop)
          return true;
        for (const auto& pair : m_workers) {
          if (pair.second.finished)
            return true;
        }
        return false;
      });

      if (m_reaper_stop)
        return;

      for (auto it = m_workers.begin(); it != m_workers.end();) {
        if (!it->second.finished) {
          ++it;
          continue;
        }
        finished.push_back(it->second.thread);
        it = m_workers.erase(it);
      }
    }

    // Never join while holding m_workers_mutex: the worker may still be
    // returning from _MarkWorkerFinished()/FinalizePipeHandle().
    for (auto& worker : finished)
      if (worker && worker->joinable())
        worker->join();
  }
}

PipeServer::ServerRunner PipeServer::GetServerRunner(
    ServerHandler const& handler) {
  return [handler, this]() { Listen(handler); };
}

void PipeServer::_ProcessPipeThread(
    WorkerId worker_id,
    HANDLE pipe,
    const std::shared_ptr<std::atomic_bool>& pipe_io_pending,
    ServerHandler const& handler) {
  try {
    for (;;) {
      Res msg;
      {
        SynchronousPipeIoScope io_scope(*pipe_io_pending);
        _Receive(pipe, &msg, sizeof(msg));
      }
      handler(msg, [this, pipe, pipe_io_pending](Msg resp) {
        SynchronousPipeIoScope io_scope(*pipe_io_pending);
        _Send(pipe, resp);
      });
    }
  } catch (...) {
  }
  // Keep thread ownership registered until an owner joins us. Mark before
  // closing the handle: if DrainWorkers wins the mutex first the handle is
  // still valid and cancellable; if it sees finished=true it skips cancellation
  // and simply joins until this close/return completes.
  _MarkWorkerFinished(worker_id);
  FinalizePipeHandle(pipe);
}

// weasel::Server

Server::Server() : m_pImpl(new ServerImpl) {}

Server::~Server() {
  if (m_pImpl)
    delete m_pImpl;
}

HWND Server::Start() {
  return m_pImpl->Start();
}

int Server::Stop() {
  return m_pImpl->Stop();
}

int Server::Run() {
  return m_pImpl->Run();
}

void Server::SetRequestHandler(RequestHandler* pHandler) {
  m_pImpl->SetRequestHandler(pHandler);
}

void Server::AddMenuHandler(UINT uID, CommandHandler handler) {
  m_pImpl->AddMenuHandler(uID, handler);
}

void Server::SetTrayRefreshCallback(std::function<void()> callback) {
  m_pImpl->SetTrayRefreshCallback(callback);
}

HWND Server::GetHWnd() {
  return m_pImpl->m_hWnd;
}
