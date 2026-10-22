// WeaselDeployer.cpp : Defines the entry point for the application.
//
#include "stdafx.h"
#include <WeaselDiagnosticTrace.h>
#include <WeaselUtility.h>
#include <WeaselIPC.h>
#include <fstream>
#include <vector>
#include "WeaselDeployer.h"
#include "Configurator.h"
#include <ShellScalingApi.h>

CAppModule _Module;

namespace {
constexpr wchar_t kInternalSyncWorkerOption[] = L"/internal-sync-worker";
constexpr wchar_t kInstallerDeployOption[] = L"/install-deploy";
constexpr DWORD kRimeOperationHardTimeoutMs = 15 * 60 * 1000;
constexpr DWORD kSyncSupervisorGraceMs = 10 * 1000;
constexpr DWORD kSyncTerminationWaitMs = 5 * 1000;

DWORD WINAPI RimeOperationWatchdogProc(LPVOID param) noexcept {
  HANDLE cancel_event = static_cast<HANDLE>(param);
  const DWORD wait_result =
      WaitForSingleObject(cancel_event, kRimeOperationHardTimeoutMs);
  CloseHandle(cancel_event);

  if (wait_result != WAIT_OBJECT_0) {
    // librime deployment work is not cancellable. Once the hard deadline
    // expires, process exit is the only boundary that can prove local Rime
    // work has stopped before WeaselServer is allowed to recover maintenance.
    TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
  }
  return 0;
}

struct RuntimeCleanup {
  bool com_initialized = false;
  bool module_initialized = false;
  RimeOperationWatchdog watchdog;

  ~RuntimeCleanup() {
    watchdog.Disarm();
    if (module_initialized)
      _Module.Term();
    if (com_initialized)
      ::CoUninitialize();
  }
};

bool IsInternalSyncWorker(int argc, wchar_t* argv[]) {
  return argc == 2 && _wcsicmp(argv[1], kInternalSyncWorkerOption) == 0;
}

std::wstring CurrentExecutablePath() {
  constexpr DWORD kPathChars = 32768;
  std::vector<wchar_t> buffer(kPathChars);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), kPathChars);
  if (!length || length >= buffer.size())
    return {};
  return std::wstring(buffer.data(), length);
}

int RunIsolatedSyncWorker() {
  const std::wstring executable = CurrentExecutablePath();
  if (executable.empty()) {
    LOG(ERROR) << "Could not resolve WeaselDeployer executable path.";
    return 1;
  }

  std::wstring command_line =
      L"\"" + executable + L"\" " + kInternalSyncWorkerOption;
  std::vector<wchar_t> mutable_command(command_line.begin(),
                                       command_line.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr,
                      nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                      &startup, &process)) {
    LOG(ERROR) << "Could not start isolated Rime sync worker, error: "
               << GetLastError();
    return 1;
  }

  CloseHandle(process.hThread);
  const DWORD wait_result = WaitForSingleObject(
      process.hProcess, kRimeOperationHardTimeoutMs + kSyncSupervisorGraceMs);
  if (wait_result == WAIT_OBJECT_0) {
    DWORD exit_code = ERROR_GEN_FAILURE;
    const bool got_exit_code =
        GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hProcess);
    if (!got_exit_code) {
      LOG(ERROR) << "Could not read isolated Rime sync worker exit code.";
      return 1;
    }
    return exit_code == 0 ? 0 : 1;
  }

  if (wait_result == WAIT_TIMEOUT) {
    LOG(ERROR) << "Isolated Rime sync worker exceeded the hard timeout.";
  } else {
    const DWORD wait_error = GetLastError();
    LOG(ERROR) << "Waiting for isolated Rime sync worker failed, error: "
               << wait_error;
  }

  // Fail closed on any supervisor wait anomaly. Do not resume maintenance
  // here; only confirmed worker exit lets WeaselServer's owner-exit recovery
  // decide whether it is safe to reinitialize Rime.
  if (!TerminateProcess(process.hProcess, ERROR_TIMEOUT)) {
    LOG(ERROR) << "Could not terminate failed Rime sync worker, error: "
               << GetLastError();
  }
  const DWORD terminated =
      WaitForSingleObject(process.hProcess, kSyncTerminationWaitMs);
  if (terminated != WAIT_OBJECT_0) {
    LOG(ERROR) << "Failed Rime sync worker did not exit promptly.";
  }

  CloseHandle(process.hProcess);
  return 1;
}
}  // namespace

RimeOperationWatchdog::~RimeOperationWatchdog() {
  Disarm();
}

bool RimeOperationWatchdog::Arm() noexcept {
  if (cancel_event_)
    return true;

  HANDLE cancel_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!cancel_event)
    return false;

  HANDLE watchdog_event = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), cancel_event, GetCurrentProcess(),
                       &watchdog_event, SYNCHRONIZE, FALSE, 0)) {
    CloseHandle(cancel_event);
    return false;
  }

  HANDLE thread = CreateThread(nullptr, 0, RimeOperationWatchdogProc,
                               watchdog_event, 0, nullptr);
  if (!thread) {
    CloseHandle(watchdog_event);
    CloseHandle(cancel_event);
    return false;
  }

  CloseHandle(thread);
  cancel_event_ = cancel_event;
  return true;
}

void RimeOperationWatchdog::Disarm() noexcept {
  if (!cancel_event_)
    return;
  SetEvent(cancel_event_);
  CloseHandle(cancel_event_);
  cancel_event_ = nullptr;
}

static int Run(int argc, wchar_t* argv[]);

static bool IsOption(const std::wstring& value, const wchar_t* option) {
  return _wcsicmp(value.c_str(), option) == 0;
}

static int ShowHelp() {
  WCHAR msg[2048] = {0};
  if (LoadString(GetModuleHandle(NULL), IDS_STR_HELP, msg,
                 sizeof(msg) / sizeof(TCHAR))) {
    std::wstring help(msg);
    help.append(
        L"\n\n/dict list - List user dictionaries\n"
        L"/dict backup <dict> - Backup a user dictionary\n"
        L"/dict restore <file> - Restore a user dictionary snapshot\n"
        L"/dict export <dict> <file> - Export a user dictionary\n"
        L"/dict import <dict> <file> - Import a user dictionary");
    MessageBox(NULL, help.c_str(), L"Weasel Deployer",
               MB_ICONINFORMATION | MB_OK);
  } else {
    MessageBox(NULL,
               L"Usage: WeaselDeployer.exe [options]\n"
               L"/? or /help\t\t- Show this help message\n"
               L"/deploy\t\t- Update Workspace\n"
               L"/dict\t\t- Manage dictionary\n"
               L"/dict list\t\t- List user dictionaries\n"
               L"/dict backup <dict>\t- Backup a user dictionary\n"
               L"/dict restore <file>\t- Restore a user dictionary snapshot\n"
               L"/dict export <dict> <file>\t- Export a user dictionary\n"
               L"/dict import <dict> <file>\t- Import a user dictionary\n"
               L"/sync\t\t- Sync user data\n"
               L"/install\t\t- Install Weasel (Initial deployment)",
               L"Weasel Deployer", MB_ICONINFORMATION | MB_OK);
  }
  return 0;
}

static bool IsDictionaryCli(int argc, wchar_t* argv[]) {
  return argc > 2 && IsOption(argv[1], L"/dict");
}

static void DetachGuiConsole() {
  HWND console = GetConsoleWindow();
  if (console) {
    DWORD processes[64] = {0};
    const DWORD count = GetConsoleProcessList(processes, _countof(processes));
    if (count == 1 && processes[0] == GetCurrentProcessId()) {
      ShowWindow(console, SW_HIDE);
    }
  }
  FreeConsole();
}

int wmain(int argc, wchar_t* argv[]) {
  weasel::diagnostics::Scope process_trace("deployer.process");
  const bool internal_sync_worker = IsInternalSyncWorker(argc, argv);
  RuntimeCleanup cleanup;
  if (internal_sync_worker && !cleanup.watchdog.Arm()) {
    LOG(ERROR) << "Could not arm Rime maintenance watchdog.";
    return 1;
  }

  if (!IsDictionaryCli(argc, argv)) {
    // GUI entry points are launched from the tray or Explorer. Keep those
    // paths silent even though the executable now uses the console subsystem
    // for /dict CLI output.
    DetachGuiConsole();
  }

  HINSTANCE hInstance = GetModuleHandle(NULL);
  LoadWeaselLogLevelFromRegistry();
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  HRESULT hRes = ::CoInitialize(NULL);
  if (FAILED(hRes)) {
    LOG(ERROR) << "CoInitialize failed: " << hRes;
    return 1;
  }
  cleanup.com_initialized = true;

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  if (FAILED(hRes)) {
    LOG(ERROR) << "ATL module initialization failed: " << hRes;
    return 1;
  }
  cleanup.module_initialized = true;

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  if (internal_sync_worker) {
    // The supervising /sync process owns the deployer-wide singleton. The
    // isolated worker intentionally skips that mutex but still owns the shared
    // ServerLifecycleLock before touching local Rime data.
    return Run(argc, argv);
  }

  int ret = 0;
  weasel::diagnostics::Trace("deployer.singleton", "begin");
  HANDLE hMutex = CreateMutex(NULL, TRUE, weasel::kDeployerExclusiveMutexName);
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerExclusiveMutex.";
    ret = 1;
  } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
    weasel::diagnostics::Trace("deployer.singleton", "already-exists");
    // 部署器可能被脚本/计划任务静默调用，只记日志不弹框，避免破坏无人值守
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    ret = 1;
  } else {
    weasel::diagnostics::Trace("deployer.singleton", "acquired");
    ret = Run(argc, argv);
  }

  if (hMutex) {
    CloseHandle(hMutex);
  }
  weasel::diagnostics::Trace("deployer.process", "result",
                             static_cast<DWORD>(ret));
  return ret;
}

static int Run(int argc, wchar_t* argv[]) {
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (args.size() == 1 &&
      (IsOption(args.front(), L"/?") || IsOption(args.front(), L"/help"))) {
    return ShowHelp();
  }

  if (args.size() == 1 && IsOption(args.front(), L"/sync")) {
    // Do not initialize librime in the supervising process. All Rime work is
    // isolated so a stuck std::async maintenance future cannot pin this
    // process, the deployer singleton, or WeaselServer maintenance forever.
    return RunIsolatedSyncWorker();
  }

  Configurator configurator;
  {
    RimeOperationWatchdog initialization_watchdog;
    if (!initialization_watchdog.Arm()) {
      LOG(ERROR) << "Could not arm Rime initialization watchdog.";
      return 1;
    }
    weasel::diagnostics::Trace("deployer.initialize", "begin");
    configurator.Initialize();
    weasel::diagnostics::Trace("deployer.initialize", "end");
  }

  if (args.empty()) {
    return configurator.Run(false);
  }

  if (IsOption(args.front(), L"/deploy") && args.size() == 1) {
    return configurator.UpdateWorkspace();
  }

  if (IsOption(args.front(), kInstallerDeployOption) && args.size() == 1) {
    // Installation registers the TSF before workspace deployment. A host that
    // loads the new TIP immediately may start WeaselServer first; wait only in
    // this installer-owned mode so that startup can finish and deployment can
    // then enter maintenance instead of failing the install-time deploy race.
    return configurator.UpdateWorkspace(false, true);
  }

  if (IsOption(args.front(), L"/dict")) {
    return configurator.DictManagement(
        std::vector<std::wstring>(args.begin() + 1, args.end()));
  }

  if (args.size() == 1 && IsOption(args.front(), kInternalSyncWorkerOption)) {
    return configurator.SyncUserData();
  }

  const bool installing =
      IsOption(args.front(), L"/install") && args.size() == 1;
  return configurator.Run(installing);
}
