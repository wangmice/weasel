// WeaselServer.cpp : main source file for WeaselServer.exe
//
//	WTL MessageLoop 封装了消息循环. 实现了 getmessage/dispatchmessage....

#include "stdafx.h"
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

bool OpenRunningServerProcess(HANDLE& process) {
  process = nullptr;
  HWND hwnd = FindWindowW(nullptr, WEASEL_IPC_WINDOW);
  if (!hwnd)
    return true;

  DWORD process_id = 0;
  GetWindowThreadProcessId(hwnd, &process_id);
  if (!process_id)
    return false;

  process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                        process_id);
  if (process)
    return true;

  // The server may have exited between FindWindowW and OpenProcess.
  return GetLastError() == ERROR_INVALID_PARAMETER;
}

bool WaitForRunningServerExit(HANDLE process) {
  if (!process)
    return true;
  const DWORD wait_result =
      WaitForSingleObject(process, kServerShutdownTimeoutMs);
  CloseHandle(process);
  return wait_result == WAIT_OBJECT_0;
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
  HANDLE running_server = nullptr;
  if (!OpenRunningServerProcess(running_server)) {
    LOG(ERROR) << "failed to retain the running WeaselServer process";
    return 1;
  }
  HWND running_window =
      running_server ? FindWindowW(nullptr, WEASEL_IPC_WINDOW) : nullptr;

  // Restart if already running. If the old window already exists while the
  // pipe listener is still starting, request shutdown through that exact
  // process's window before waiting for process exit; never wait on a live
  // process that has not actually been asked to stop.
  {
    weasel::Client client;
    const bool connected = client.Connect();
    if (connected) {
      // A server can appear between the first window probe and Connect(). If
      // that happened, capture it now so the restart still has a process-exit
      // barrier rather than reverting to the old pipe-only race.
      if (!running_server) {
        if (!OpenRunningServerProcess(running_server) || !running_server) {
          LOG(ERROR)
              << "connected to WeaselServer but could not retain its process";
          client.Disconnect();
          return 1;
        }
        running_window = FindWindowW(nullptr, WEASEL_IPC_WINDOW);
      }
      client.ShutdownServer();
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
          // The old process may have exited between the zero-time wait and the
          // window operation. Treat that race as a successful shutdown rather
          // than failing a perfectly safe replacement.
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
      // Release our pipe before waiting: server teardown drains pipe workers,
      // so keeping this client connected would only delay shutdown.
      client.Disconnect();
      if (!WaitForRunningServerExit(running_server)) {
        LOG(ERROR) << "timed out waiting for WeaselServer to exit";
        return 1;
      }
      if (quit)
        return 0;
    } else if (quit) {
      return 0;
    }
  }

  bool check_updates = !wcscmp(L"/update", lpstrCmdLine);
  if (check_updates) {
    WeaselServerApp::check_update();
  }

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int nRet = 0;
  try {
    LOG(INFO) << "constructing WeaselServerApp";
    WeaselServerApp app;
    RegisterApplicationRestart(NULL, 0);
    LOG(INFO) << "starting WeaselServerApp";
    nRet = app.Run();
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
