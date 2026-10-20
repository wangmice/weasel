// WeaselSetup.cpp : main source file for WeaselSetup.exe
//

#include "stdafx.h"

#include "resource.h"
#include "WeaselUtility.h"
#include <WeaselIPC.h>
#include "PerUserReg.h"
#include "RegistrationCleanup.h"
#include "SetupUtil.h"
#include <vector>

#include "InstallOptionsDlg.h"
#include "TransactionalInstall.h"

#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
CAppModule _Module;

static int Run(LPTSTR lpCmdLine);
static bool IsProcAdmin();
static int RestartAsAdmin(LPTSTR lpCmdLine);

int WINAPI _tWinMain(HINSTANCE hInstance,
                     HINSTANCE /*hPrevInstance*/,
                     LPTSTR lpstrCmdLine,
                     int /*nCmdShow*/) {
  HRESULT hRes = ::CoInitialize(NULL);
  ATLASSERT(SUCCEEDED(hRes));
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  int nRet = Run(lpstrCmdLine);

  _Module.Term();
  ::CoUninitialize();

  return nRet;
}
int install(const std::wstring& profile, bool silent);
int uninstall(bool silent);
bool has_installed();
// 已安装状态下切换 profile 时同步 TSF 注册（K14）
int switch_registered_profile(const std::wstring& old_profile,
                              const std::wstring& new_profile,
                              bool silent);

static int InstallRequestedProfile(const std::wstring& profile,
                                   const std::wstring& user_dir,
                                   bool silent) {
  if (has_installed())
    return install(profile, silent);
  return weasel::setup::InstallFreshTransactional(profile, user_dir, silent);
}

static std::wstring install_dir() {
  WCHAR exe_path[MAX_PATH] = {0};
  GetModuleFileNameW(GetModuleHandle(NULL), exe_path, _countof(exe_path));
  std::wstring dir(exe_path);
  size_t pos = dir.find_last_of(L"\\");
  dir.resize(pos);
  return dir;
}

namespace {
constexpr DWORD kServerTransitionTimeoutMs = 30000;
constexpr DWORD kDeployerOperationTimeoutMs = 15 * 60 * 1000 + 10 * 1000;
constexpr DWORD kDeployerTerminationWaitMs = 5000;

enum class DeployerRunResult {
  Success,
  FailedStopped,
  FailedStillRunning,
};

bool RunProcessAndWait(const std::wstring& executable,
                       const std::wstring& parameters,
                       DWORD timeout_ms,
                       DWORD& exit_code,
                       int show = SW_SHOWNORMAL) {
  SHELLEXECUTEINFOW exec{};
  exec.cbSize = sizeof(exec);
  exec.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
  exec.lpFile = executable.c_str();
  exec.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
  exec.nShow = show;
  if (!ShellExecuteExW(&exec) || !exec.hProcess)
    return false;

  const DWORD wait_result = WaitForSingleObject(exec.hProcess, timeout_ms);
  bool ok = wait_result == WAIT_OBJECT_0;
  exit_code = ERROR_GEN_FAILURE;
  if (ok && !GetExitCodeProcess(exec.hProcess, &exit_code))
    ok = false;
  CloseHandle(exec.hProcess);
  return ok;
}

bool OpenRunningServerProcess(HANDLE& process) {
  process = nullptr;
  HWND hwnd = FindWindowW(nullptr, WEASEL_IPC_WINDOW);
  if (!hwnd)
    return true;

  DWORD process_id = 0;
  GetWindowThreadProcessId(hwnd, &process_id);
  if (!process_id)
    return false;
  process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
  if (process)
    return true;
  // The server may have exited between FindWindow and OpenProcess. Treat that
  // race as an already-stopped server; other access failures remain errors.
  return GetLastError() == ERROR_INVALID_PARAMETER;
}

HANDLE LaunchProcess(const std::wstring& executable,
                     const std::wstring& parameters) {
  SHELLEXECUTEINFOW exec{};
  exec.cbSize = sizeof(exec);
  exec.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
  exec.lpFile = executable.c_str();
  exec.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
  exec.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&exec) || !exec.hProcess)
    return nullptr;
  return exec.hProcess;
}

bool WaitForServerReady(HANDLE server_process, DWORD timeout_ms) {
  const std::wstring pipe_name = weasel::GetPipeName();
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    if (WaitForSingleObject(server_process, 0) == WAIT_OBJECT_0)
      return false;
    if (WaitNamedPipeW(pipe_name.c_str(), 100))
      return true;
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_SEM_TIMEOUT &&
        error != ERROR_PIPE_BUSY) {
      return false;
    }
    Sleep(50);
  }
  return false;
}

bool RestartServer(const std::wstring& dir) {
  const std::wstring server = dir + L"\\WeaselServer.exe";

  HANDLE old_server = nullptr;
  if (!OpenRunningServerProcess(old_server))
    return false;
  if (old_server) {
    DWORD quit_exit_code = 0;
    const bool quit_ok =
        RunProcessAndWait(server, L"/q", kServerTransitionTimeoutMs,
                          quit_exit_code) &&
        quit_exit_code == 0;
    if (!quit_ok) {
      CloseHandle(old_server);
      return false;
    }
    const DWORD stopped =
        WaitForSingleObject(old_server, kServerTransitionTimeoutMs);
    CloseHandle(old_server);
    if (stopped != WAIT_OBJECT_0)
      return false;
  }

  HANDLE new_server = LaunchProcess(server, L"");
  if (!new_server)
    return false;
  const bool server_ready =
      WaitForServerReady(new_server, kServerTransitionTimeoutMs);
  CloseHandle(new_server);
  return server_ready;
}

DeployerRunResult RunDeployer(const std::wstring& dir) {
  const std::wstring deployer = dir + L"\\WeaselDeployer.exe";
  std::wstring command_line = L"\"" + deployer + L"\" /deploy";
  std::vector<wchar_t> mutable_command(command_line.begin(),
                                       command_line.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process_info{};
  if (!CreateProcessW(deployer.c_str(), mutable_command.data(), nullptr,
                      nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                      &startup, &process_info)) {
    LOG(ERROR) << "Could not launch WeaselDeployer, error: " << GetLastError();
    return DeployerRunResult::FailedStopped;
  }

  CloseHandle(process_info.hThread);
  HANDLE process = process_info.hProcess;

  const DWORD wait_result =
      WaitForSingleObject(process, kDeployerOperationTimeoutMs);
  if (wait_result == WAIT_OBJECT_0) {
    DWORD exit_code = ERROR_GEN_FAILURE;
    const bool got_exit_code = GetExitCodeProcess(process, &exit_code) != FALSE;
    CloseHandle(process);
    if (!got_exit_code) {
      LOG(ERROR) << "Could not read WeaselDeployer exit code.";
      return DeployerRunResult::FailedStopped;
    }
    return exit_code == 0 ? DeployerRunResult::Success
                          : DeployerRunResult::FailedStopped;
  }

  if (wait_result == WAIT_TIMEOUT) {
    LOG(ERROR) << "WeaselDeployer exceeded the deployment timeout.";
  } else {
    const DWORD wait_error = GetLastError();
    LOG(ERROR) << "Waiting for WeaselDeployer failed, error: " << wait_error;
  }

  // Deployment mutates Rime data while owning server maintenance. Never roll
  // back registry/profile state or restart the server until the deployer is
  // confirmed dead. If termination cannot be confirmed, fail closed and leave
  // the existing maintenance owner in charge of recovery.
  if (!TerminateProcess(process, ERROR_TIMEOUT)) {
    LOG(ERROR) << "Could not terminate failed WeaselDeployer, error: "
               << GetLastError();
  }

  const DWORD stopped =
      WaitForSingleObject(process, kDeployerTerminationWaitMs);
  CloseHandle(process);
  if (stopped != WAIT_OBJECT_0) {
    LOG(ERROR) << "WeaselDeployer is still running after termination request; "
                  "skipping rollback and server restart.";
    return DeployerRunResult::FailedStillRunning;
  }

  return DeployerRunResult::FailedStopped;
}
}  // namespace

static int CustomInstall(bool installing) {
  std::wstring profile = L"hans";
  bool silent = false;
  std::wstring user_dir;

  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  LSTATUS ret =
      RegOpenKeyW(per_user_root(), per_user_subkey(KEY).c_str(), &hKey);
  if (ret == ERROR_SUCCESS) {
    std::wstring stored;
    if (read_reg_sz(hKey, L"RimeUserDir", stored)) {
      user_dir = stored;
    }
    if (read_reg_sz(hKey, L"Profile", stored) && !stored.empty()) {
      profile = stored;
      if (installing)
        silent = true;
    } else {
      DWORD data = 0;
      DWORD len = sizeof(data);
      DWORD type = 0;
      ret = RegQueryValueEx(hKey, L"Hant", NULL, &type, (LPBYTE)&data, &len);
      if (ret == ERROR_SUCCESS && type == REG_DWORD) {
        profile = (data != 0) ? L"hant" : L"hans";
        if (installing)
          silent = true;
      }
    }
    RegCloseKey(hKey);
  }
  // TSF 注册所用的 profile 与此持久化值一致（install() 同时写两者），
  // 作为修改安装时判断 profile 是否变化的基准
  const std::wstring registered_profile = profile;
  bool _has_installed = has_installed();
  if (!silent) {
    InstallOptionsDialog dlg;
    dlg.installed = _has_installed;
    dlg.profile = profile;
    dlg.user_dir = user_dir;
    if (IDOK != dlg.DoModal())
      return ERROR_CANCELLED;  // 与安装失败区分，外层安装器必须中止
    profile = dlg.profile;
    user_dir = dlg.user_dir;
    _has_installed = dlg.installed;
  }

  if (user_dir.empty())
    user_dir = per_user_default_dir();

  if (!_has_installed) {
    if (0 != InstallRequestedProfile(profile, user_dir, silent))
      return 1;
    // Fresh install committed Profile/Hant/RimeUserDir atomically with the
    // TSF registration and layout. Do not perform a second fallible write pass.
    return 0;
  }

  const std::wstring user_key = per_user_subkey(KEY);
  std::vector<weasel::setup::detail::RegistryValueSnapshot> registry = {
      {per_user_root(), user_key, L"RimeUserDir"},
      {per_user_root(), user_key, L"Profile"},
      {per_user_root(), user_key, L"Hant"},
  };
  for (auto& snapshot : registry) {
    if (!snapshot.Capture())
      return 1;
  }
  auto restore_registry = [&]() {
    for (auto it = registry.rbegin(); it != registry.rend(); ++it)
      it->Restore();
  };
  bool profile_switched = false;
  auto rollback_modify_state = [&]() {
    if (profile_switched &&
        switch_registered_profile(profile, registered_profile, true) != 0) {
      // switch_registered_profile is itself transactional. A failed reverse
      // switch leaves the new profile state in place; keep the matching new
      // registry values rather than creating a registry/TSF split-brain state.
      LOG(ERROR) << "failed to restore the previous TSF profile after "
                    "modify-install failure";
      return false;
    }
    restore_registry();
    return true;
  };

  ret = SetRegKeyValue(per_user_root(), user_key.c_str(), L"RimeUserDir",
                       user_dir.c_str(), REG_SZ, false);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    restore_registry();
    MSG_BY_IDS(IDS_STR_ERR_WRITE_USER_DIR, IDS_STR_INSTALL_FAILED,
               MB_ICONERROR | MB_OK);
    return 1;
  }
  ret = SetRegKeyValue(per_user_root(), user_key.c_str(), L"Profile",
                       profile.c_str(), REG_SZ, false);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    restore_registry();
    MSG_BY_IDS(IDS_STR_ERR_WRITE_PROFILE, IDS_STR_INSTALL_FAILED,
               MB_ICONERROR | MB_OK);
    return 1;
  }

  ret = SetRegKeyValue(per_user_root(), user_key.c_str(), L"Hant",
                       (profile == L"hant" ? 1 : 0), REG_DWORD, false);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    restore_registry();
    MSG_BY_IDS(IDS_STR_ERR_WRITE_HANT, IDS_STR_INSTALL_FAILED,
               MB_ICONERROR | MB_OK);
    return 1;
  }

  if (profile != registered_profile) {
    // Persist the requested state first. The profile switch has its own exact
    // TSF/layout rollback; if it fails, restore the registry snapshot too so
    // both sources of truth remain aligned.
    if (0 != switch_registered_profile(registered_profile, profile, silent)) {
      restore_registry();
      return 1;
    }
    profile_switched = true;
  }

  std::wstring dir(install_dir());
  if (!RestartServer(dir)) {
    (void)rollback_modify_state();
    return 1;
  }
  const DeployerRunResult deploy_result = RunDeployer(dir);
  if (deploy_result != DeployerRunResult::Success) {
    if (deploy_result == DeployerRunResult::FailedStillRunning) {
      // A live deployer may still own maintenance and mutate Rime data. Do not
      // restore registry/profile state or restart WeaselServer underneath it.
      return 1;
    }

    (void)rollback_modify_state();
    // The deployer is confirmed stopped. Restore the previous registry/profile
    // state, then restart the server so its process-local paths and Rime state
    // no longer reference the failed modification.
    (void)RestartServer(dir);
    return 1;
  }
  MSG_BY_IDS(IDS_STR_MODIFY_SUCCESS_INFO, IDS_STR_MODIFY_SUCCESS_CAP,
             MB_ICONINFORMATION | MB_OK);

  return 0;
}

LPCTSTR GetParamByPrefix(LPCTSTR lpCmdLine, LPCTSTR prefix) {
  return (wcsncmp(lpCmdLine, prefix, wcslen(prefix)) == 0)
             ? (lpCmdLine + wcslen(prefix))
             : 0;
}

static int Run(LPTSTR lpCmdLine) {
  // "/origsid:<S-...>" is appended by RestartAsAdmin so the elevated process
  // can address the invoking user's hive. Strip the trailing parameter
  // before any command-line matching.
  apply_orig_sid_param(lpCmdLine);
  constexpr bool silent = true;
  // parameter /? or /help to show commandline args
  if (!wcscmp(L"/?", lpCmdLine) || !wcscmp(L"/help", lpCmdLine)) {
    WCHAR msg[1024] = {0};
    if (LoadString(GetModuleHandle(NULL), IDS_STR_HELP, msg,
                   sizeof(msg) / sizeof(TCHAR))) {
      MessageBox(NULL, msg, L"WeaselSetup", MB_ICONINFORMATION | MB_OK);
    } else {
      MessageBox(
          NULL,
          L"Usage: WeaselSetup.exe [options]\n"
          L"/? or /help    - Show this help message\n"
          L"/u             - Uninstall Weasel\n"
          L"/i             - Install Weasel\n"
          L"/s             - Install Weasel (Simplified Chinese)\n"
          L"/t             - Install Weasel (Traditional Chinese)\n"
          L"/hk            - Install Weasel (Traditional Chinese, Hong Kong)\n"
          L"/mc            - Install Weasel (Traditional Chinese, Macao)\n"
          L"/sg            - Install Weasel (Simplified Chinese, Singapore)\n"
          L"/ls            - Set Weasel language to Simplified Chinese\n"
          L"/lt            - Set Weasel language to Traditional Chinese\n"
          L"/le            - Set Weasel language to English\n"
          L"/eu            - Enable automatic update check\n"
          L"/du            - Disable automatic update check\n"
          L"/toggleime     - Toggle IME on open/close(ctrl+space)\n"
          L"/toggleascii   - Toggle ASCII on open/close(ctrl+space)\n"
          L"/testing       - Set update channel to testing\n"
          L"/release       - Set update channel to release\n"
          L"/userdir:<dir> - Set user directory\n",
          L"WeaselSetup", MB_ICONINFORMATION | MB_OK);
    }
    return 0;
  }
  bool uninstalling = !wcscmp(L"/u", lpCmdLine);
  if (uninstalling) {
    if (IsProcAdmin()) {
      const int result = uninstall(silent);
      if (result != 0)
        return result;
      return weasel::setup::CleanupTextServiceRegistrationAfterUninstall() ? 0
                                                                           : 1;
    } else {
      return RestartAsAdmin(lpCmdLine);
    }
  }

  if (auto res = GetParamByPrefix(lpCmdLine, L"/userdir:")) {
    // 剥离 /userdir:"..." 的引号；返回明确的成功/失败退出码（K15）
    const LSTATUS ret =
        SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                       L"RimeUserDir", unquote_argument(res), REG_SZ);
    return ret == ERROR_SUCCESS ? 0 : 1;
  }

  if (!wcscmp(L"/ls", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"Language", L"chs", REG_SZ);
  } else if (!wcscmp(L"/lt", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"Language", L"cht", REG_SZ);
  } else if (!wcscmp(L"/le", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"Language", L"eng", REG_SZ);
  }

  if (!wcscmp(L"/eu", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel\\Updates",
                          L"CheckForUpdates", L"1", REG_SZ);
  }
  if (!wcscmp(L"/du", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel\\Updates",
                          L"CheckForUpdates", L"0", REG_SZ);
  }

  if (!wcscmp(L"/toggleime", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"ToggleImeOnOpenClose", L"yes", REG_SZ);
  }
  if (!wcscmp(L"/toggleascii", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"ToggleImeOnOpenClose", L"no", REG_SZ);
  }
  if (!wcscmp(L"/testing", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"UpdateChannel", L"testing", REG_SZ);
  }
  if (!wcscmp(L"/release", lpCmdLine)) {
    return SetRegKeyValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                          L"UpdateChannel", L"release", REG_SZ);
  }

  if (!IsProcAdmin()) {
    return RestartAsAdmin(lpCmdLine);
  }

  bool hans = !wcscmp(L"/s", lpCmdLine);
  if (hans)
    return InstallRequestedProfile(L"hans", L"", silent);
  bool hant = !wcscmp(L"/t", lpCmdLine);
  if (hant)
    return InstallRequestedProfile(L"hant", L"", silent);
  bool hongkong = !wcscmp(L"/hk", lpCmdLine);
  if (hongkong)
    return InstallRequestedProfile(L"hongkong", L"", silent);
  bool macau = !wcscmp(L"/mc", lpCmdLine);
  if (macau)
    return InstallRequestedProfile(L"macau", L"", silent);
  bool singapore = !wcscmp(L"/sg", lpCmdLine);
  if (singapore)
    return InstallRequestedProfile(L"singapore", L"", silent);
  bool installing = !wcscmp(L"/i", lpCmdLine);
  return CustomInstall(installing);
}

// https://learn.microsoft.com/zh-cn/windows/win32/api/securitybaseapi/nf-securitybaseapi-checktokenmembership
bool IsProcAdmin() {
  BOOL b = FALSE;
  SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
  PSID AdministratorsGroup;
  b = AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &AdministratorsGroup);

  if (b) {
    if (!CheckTokenMembership(NULL, AdministratorsGroup, &b)) {
      b = FALSE;
    }
    FreeSid(AdministratorsGroup);
  }

  return (b);
}

int RestartAsAdmin(LPTSTR lpCmdLine) {
  SHELLEXECUTEINFO execInfo{0};
  TCHAR path[MAX_PATH];
  GetModuleFileName(GetModuleHandle(NULL), path, _countof(path));
  std::wstring params = with_orig_sid_param(lpCmdLine);
  execInfo.lpFile = path;
  execInfo.lpParameters = params.c_str();
  execInfo.lpVerb = _T("runas");
  execInfo.cbSize = sizeof(execInfo);
  execInfo.nShow = SW_SHOWNORMAL;
  execInfo.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
  execInfo.hwnd = NULL;
  execInfo.hProcess = NULL;
  if (::ShellExecuteEx(&execInfo) && execInfo.hProcess != NULL) {
    ::WaitForSingleObject(execInfo.hProcess, INFINITE);
    DWORD dwExitCode = 0;
    ::GetExitCodeProcess(execInfo.hProcess, &dwExitCode);
    ::CloseHandle(execInfo.hProcess);
    return dwExitCode;
  }
  return -1;
}
