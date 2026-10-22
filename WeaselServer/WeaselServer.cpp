// WeaselServer.cpp : main source file for WeaselServer.exe
//
//	WTL MessageLoop 封装了消息循环. 实现了 getmessage/dispatchmessage....

#include "stdafx.h"
#include <WeaselDiagnosticTrace.h>
#include "resource.h"
#include "WeaselServerApp.h"
#include <WeaselIPC.h>
#include <WeaselUI.h>
#include <RimeWithWeasel.h>
#include <WeaselUtility.h>
#include <winsparkle.h>
#include <functional>
#include <ShellScalingApi.h>
#include <WinUser.h>
#include <memory>
#include <atlstr.h>
#pragma comment(lib, "Shcore.lib")
CAppModule _Module;

namespace {
constexpr DWORD kServerShutdownTimeoutMs = 30000;
constexpr DWORD kStartingServerForcedExitTimeoutMs = 2000;

struct RuntimeCleanup {
  bool com_initialized = false;
  bool module_initialized = false;

  ~RuntimeCleanup() {
    if (module_initialized)
      _Module.Term();
    if (com_initialized)
      ::CoUninitialize();
  }
};

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

bool QueryProcessImage(HANDLE process, std::wstring& image) noexcept {
  wchar_t path[32768] = {};
  DWORD length = static_cast<DWORD>(_countof(path));
  if (!QueryFullProcessImageNameW(process, 0, path, &length))
    return false;
  image.assign(path, length);
  return true;
}

bool IsCurrentSessionServerProcess(HANDLE process,
                                   DWORD process_id,
                                   UINT64 expected_creation_time) noexcept {
  DWORD current_session = 0;
  DWORD process_session = 0;
  if (!weasel::GetCurrentWindowsSessionId(current_session) ||
      !ProcessIdToSessionId(process_id, &process_session) ||
      process_session != current_session) {
    return false;
  }

  if (expected_creation_time) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
      return false;
    ULARGE_INTEGER creation{};
    creation.LowPart = created.dwLowDateTime;
    creation.HighPart = created.dwHighDateTime;
    if (creation.QuadPart != expected_creation_time)
      return false;
  }

  std::wstring candidate_image;
  std::wstring current_image;
  return QueryProcessImage(process, candidate_image) &&
         QueryProcessImage(GetCurrentProcess(), current_image) &&
         _wcsicmp(candidate_image.c_str(), current_image.c_str()) == 0;
}

HWND FindServerWindowForProcess(DWORD process_id) noexcept {
  HWND window = nullptr;
  while ((window = FindWindowExW(nullptr, window, nullptr,
                                 WEASEL_IPC_WINDOW)) != nullptr) {
    DWORD window_process_id = 0;
    if (GetWindowThreadProcessId(window, &window_process_id) &&
        window_process_id == process_id) {
      return window;
    }
  }
  return nullptr;
}

bool OpenValidatedServerProcess(DWORD process_id,
                                UINT64 expected_creation_time,
                                HANDLE& process) noexcept {
  process =
      OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION |
                      PROCESS_TERMINATE,
                  FALSE, process_id);
  if (!process)
    return GetLastError() == ERROR_INVALID_PARAMETER;

  if (IsCurrentSessionServerProcess(process, process_id,
                                    expected_creation_time)) {
    return true;
  }

  CloseHandle(process);
  process = nullptr;
  return false;
}

bool OpenRunningServerProcess(
    HANDLE& process,
    HWND& window,
    weasel::ServerRuntimeState* runtime_state = nullptr) {
  process = nullptr;
  window = nullptr;
  if (runtime_state)
    *runtime_state = {};

  weasel::ServerRuntimeState state{};
  if (ReadServerRuntimeState(state)) {
    if (!OpenValidatedServerProcess(state.process_id,
                                    state.process_creation_time, process)) {
      return false;
    }
    if (process)
      window = FindServerWindowForProcess(state.process_id);
    if (runtime_state)
      *runtime_state = state;
    return true;
  }

  // Upgrade compatibility: pre-runtime-state servers are accepted only when
  // the window belongs to this exact WeaselServer image in the current Windows
  // session. The title is a discovery hint, never the identity root.
  HWND candidate = nullptr;
  while ((candidate = FindWindowExW(nullptr, candidate, nullptr,
                                    WEASEL_IPC_WINDOW)) != nullptr) {
    DWORD process_id = 0;
    if (!GetWindowThreadProcessId(candidate, &process_id) || !process_id)
      continue;

    HANDLE candidate_process = nullptr;
    if (!OpenValidatedServerProcess(process_id, 0, candidate_process))
      continue;
    if (!candidate_process)
      continue;

    process = candidate_process;
    window = candidate;
    return true;
  }

  return true;
}

bool WaitForRunningServerExit(HANDLE process) {
  if (!process)
    return true;
  const DWORD wait_result =
      WaitForSingleObject(process, kServerShutdownTimeoutMs);
  CloseHandle(process);
  return wait_result == WAIT_OBJECT_0;
}

bool SameServerGeneration(const weasel::ServerRuntimeState& lhs,
                          const weasel::ServerRuntimeState& rhs) noexcept {
  return lhs.version == rhs.version && lhs.process_id == rhs.process_id &&
         lhs.process_creation_time == rhs.process_creation_time;
}

bool ForceStopStartingServer(
    HANDLE process,
    HWND window,
    const weasel::ServerRuntimeState& expected_state) noexcept {
  if (!process ||
      expected_state.lifecycle !=
          static_cast<LONG>(weasel::ServerLifecycle::Starting)) {
    return false;
  }

  weasel::ServerRuntimeState current{};
  if (!ReadServerRuntimeState(current) ||
      !SameServerGeneration(current, expected_state) ||
      current.lifecycle !=
          static_cast<LONG>(weasel::ServerLifecycle::Starting)) {
    return false;
  }

  if (window)
    PostMessageW(window, WM_CLOSE, 0, 0);
  if (WaitForSingleObject(process, kStartingServerForcedExitTimeoutMs) ==
      WAIT_OBJECT_0) {
    return true;
  }

  if (!ReadServerRuntimeState(current) ||
      !SameServerGeneration(current, expected_state) ||
      current.lifecycle !=
          static_cast<LONG>(weasel::ServerLifecycle::Starting)) {
    return false;
  }

  if (!TerminateProcess(process, ERROR_TIMEOUT))
    return false;
  return WaitForSingleObject(process, kStartingServerForcedExitTimeoutMs) ==
         WAIT_OBJECT_0;
}
}  // namespace

int WINAPI _tWinMain(HINSTANCE hInstance,
                     HINSTANCE /*hPrevInstance*/,
                     LPTSTR lpstrCmdLine,
                     int nCmdShow) {
  LoadWeaselLogLevelFromRegistry();
  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  if (!IsWindowsBlueOrLaterEx()) {
    CString info, cap;
    info.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING);
    cap.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING_CAPTION);
    MessageBoxExW(NULL, info, cap, MB_ICONERROR, langId);
    return 0;
  }
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  // 防止服务进程开启输入法
  ImmDisableIME(-1);

  // 拒绝在 SYSTEM 账户下运行；getUsername 动态长度且失败返回空串，
  // 超长用户名不再因固定缓冲读取失败而被静默放行
  if (!_wcsicmp(getUsername().c_str(), L"SYSTEM")) {
    return 1;
  }

  RuntimeCleanup cleanup;
  HRESULT hRes = ::CoInitialize(NULL);
  if (FAILED(hRes)) {
    LOG(ERROR) << "CoInitialize failed: " << hRes;
    return -1;
  }
  cleanup.com_initialized = true;
  // If you are running on NT 4.0 or higher you can use the following call
  // instead to make the EXE free threaded. This means that calls come in on a
  // random RPC thread.
  // HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hRes));

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  if (FAILED(hRes)) {
    LOG(ERROR) << "ATL module initialization failed: " << hRes;
    return -1;
  }
  cleanup.module_initialized = true;

  if (!wcscmp(L"/userdir", lpstrCmdLine)) {
    CreateDirectory(WeaselUserDataPath().c_str(), NULL);
    WeaselServerApp::explore(WeaselUserDataPath());
    return 0;
  }
  if (!wcscmp(L"/weaseldir", lpstrCmdLine)) {
    WeaselServerApp::explore(WeaselServerApp::install_dir());
    return 0;
  }
  if (!wcscmp(L"/ascii", lpstrCmdLine) || !wcscmp(L"/nascii", lpstrCmdLine)) {
    weasel::Client client;
    bool ascii = !wcscmp(L"/ascii", lpstrCmdLine);
    if (client.Connect())  // try to connect to running server
    {
      if (ascii)
        client.TrayCommand(ID_WEASELTRAY_ENABLE_ASCII);
      else
        client.TrayCommand(ID_WEASELTRAY_DISABLE_ASCII);
    }
    return 0;
  }

  // Retain the existing process for both /quit and a normal restart. The pipe
  // disappears before process teardown releases ServerImpl's singleton mutex,
  // so pipe disappearance alone is not a safe restart boundary.
  bool quit = !wcscmp(L"/q", lpstrCmdLine) || !wcscmp(L"/quit", lpstrCmdLine);
  weasel::diagnostics::Trace("server-main", quit ? "quit" : "start");
  HANDLE running_server = nullptr;
  HWND running_window = nullptr;
  weasel::ServerRuntimeState running_state{};
  if (!OpenRunningServerProcess(running_server, running_window,
                                &running_state)) {
    LOG(ERROR) << "failed to validate the running WeaselServer process";
    return 1;
  }

  // Restart if already running. Lifecycle ownership is needed only when an
  // existing server will be stopped. A recovery-launched replacement with no
  // old server must remain free to reach ServerImpl::Start(), where it creates
  // its singleton before waiting for the recovery worker's lifecycle lock.
  {
    weasel::Client client;
    const bool connected = client.Connect();

    weasel::ServerLifecycleLock restart_lifecycle;
    if ((connected || running_server) && !restart_lifecycle.Acquire(0)) {
      weasel::diagnostics::Trace("server-main.lifecycle", "busy");
      const bool recovered_starting =
          running_server &&
          running_state.lifecycle ==
              static_cast<LONG>(weasel::ServerLifecycle::Starting) &&
          ForceStopStartingServer(running_server, running_window,
                                  running_state);
      if (!recovered_starting) {
        LOG(WARNING)
            << "another server lifecycle operation is active; refusing "
               "to restart or quit WeaselServer";
        client.Disconnect();
        if (running_server)
          CloseHandle(running_server);
        return 1;
      }

      client.Disconnect();
      CloseHandle(running_server);
      running_server = nullptr;
      running_window = nullptr;
      running_state = {};
      if (quit)
        return 0;

      if (!restart_lifecycle.Acquire(kStartingServerForcedExitTimeoutMs)) {
        LOG(WARNING) << "lifecycle ownership changed after recovering a stuck "
                        "Starting server";
        return 1;
      }
    }

    if (connected) {
      // A server can appear between the first window probe and Connect(). If
      // that happened, capture it now so the restart still has a process-exit
      // barrier rather than reverting to the old pipe-only race.
      if (!running_server) {
        if (!OpenRunningServerProcess(running_server, running_window,
                                      &running_state) ||
            !running_server) {
          LOG(ERROR)
              << "connected to WeaselServer but could not validate its process";
          client.Disconnect();
          return 1;
        }
      }
      client.ShutdownServer();

      // The pipe shutdown request can queue behind a worker stuck in librime
      // under g_api_mutex. The server window is authenticated to this exact
      // process generation above, so also request control-plane shutdown via
      // the message thread. New servers handle WM_CLOSE without waiting on the
      // API mutex; older servers still fall back to the bounded process wait.
      DWORD window_process_id = 0;
      const DWORD server_process_id = GetProcessId(running_server);
      if (running_window && server_process_id &&
          GetWindowThreadProcessId(running_window, &window_process_id) &&
          window_process_id == server_process_id) {
        PostMessageW(running_window, WM_CLOSE, 0, 0);
      }
    } else if (running_server) {
      const DWORD process_state = WaitForSingleObject(running_server, 0);
      if (process_state == WAIT_OBJECT_0) {
        CloseHandle(running_server);
        running_server = nullptr;
        running_window = nullptr;
        if (quit)
          return 0;
      } else if (process_state == WAIT_FAILED) {
        LOG(ERROR) << "failed to query running WeaselServer state, error: "
                   << GetLastError();
        CloseHandle(running_server);
        return 1;
      } else {
        DWORD window_process_id = 0;
        const DWORD server_process_id = GetProcessId(running_server);
        const bool owns_window =
            running_window && server_process_id &&
            GetWindowThreadProcessId(running_window, &window_process_id) &&
            window_process_id == server_process_id;
        if (!owns_window || !PostMessageW(running_window, WM_CLOSE, 0, 0)) {
          if (WaitForSingleObject(running_server, 0) == WAIT_OBJECT_0) {
            CloseHandle(running_server);
            running_server = nullptr;
            running_window = nullptr;
            if (quit)
              return 0;
          } else {
            LOG(ERROR) << "WeaselServer pipe is not ready and window shutdown "
                          "failed, error: "
                       << GetLastError();
            CloseHandle(running_server);
            return 1;
          }
        }
      }
    }

    if (running_server) {
      weasel::diagnostics::Trace("server-main.shutdown", "wait-begin");
      client.Disconnect();
      if (!WaitForRunningServerExit(running_server)) {
        weasel::diagnostics::Trace("server-main.shutdown", "wait-failed");
        LOG(ERROR) << "timed out waiting for WeaselServer to exit";
        return 1;
      }
      if (quit)
        return 0;
    } else if (quit) {
      return 0;
    }
    // restart_lifecycle is released before constructing the replacement.
    // ServerImpl immediately publishes its singleton before reacquiring the
    // same lifecycle mutex, so deploy/recovery cannot overlap its
    // initialization.
  }

  const bool check_updates = !wcscmp(L"/update", lpstrCmdLine);

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int nRet = 0;
  try {
    LOG(INFO) << "constructing WeaselServerApp";
    WeaselServerApp app;
    RegisterApplicationRestart(NULL, 0);
    LOG(INFO) << "starting WeaselServerApp";
    nRet = app.Run(check_updates);
    LOG(INFO) << "WeaselServerApp::Run returned: " << nRet;
  } catch (DWORD error) {
    LOG(ERROR) << "WeaselServer failed with Win32 error: " << error;
    nRet = -1;
  } catch (const std::exception& ex) {
    LOG(ERROR) << "WeaselServer failed with exception: " << ex.what();
    nRet = -1;
  } catch (...) {
    LOG(ERROR) << "WeaselServer failed with unknown exception";
    nRet = -1;
  }

  return nRet;
}
