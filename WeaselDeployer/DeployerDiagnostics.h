#pragma once

#include <strsafe.h>
#include <windows.h>

class DeployerDiagnosticLastErrorGuard {
 public:
  DeployerDiagnosticLastErrorGuard() noexcept : last_error_(GetLastError()) {}
  DeployerDiagnosticLastErrorGuard(const DeployerDiagnosticLastErrorGuard&) =
      delete;
  DeployerDiagnosticLastErrorGuard& operator=(
      const DeployerDiagnosticLastErrorGuard&) = delete;
  ~DeployerDiagnosticLastErrorGuard() { SetLastError(last_error_); }

 private:
  DWORD last_error_;
};

// Best-effort bootstrap diagnostics for installer/deployer failures. This helper
// deliberately has no control-flow effect: logging failures are ignored and no
// waits, retries, or synchronization are introduced. Preserve LastError because
// the deployer uses it immediately after named-mutex creation.
inline void AppendDeployerDiagnostic(const wchar_t* message) noexcept {
  DeployerDiagnosticLastErrorGuard last_error_guard;

  WCHAR temp_path[MAX_PATH] = {};
  const DWORD temp_len = GetTempPathW(_countof(temp_path), temp_path);
  if (temp_len == 0 || temp_len >= _countof(temp_path))
    return;

  WCHAR log_path[MAX_PATH] = {};
  if (FAILED(StringCchPrintfW(log_path, _countof(log_path),
                              L"%sweasel-deployer-bootstrap.log", temp_path))) {
    return;
  }

  HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;

  SYSTEMTIME now = {};
  GetLocalTime(&now);
  WCHAR line[1536] = {};
  if (SUCCEEDED(StringCchPrintfW(
          line, _countof(line),
          L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu %s\r\n",
          now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
          now.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(),
          message ? message : L""))) {
    char utf8[8192] = {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8,
                                          static_cast<int>(_countof(utf8)),
                                          nullptr, nullptr);
    if (bytes > 1) {
      DWORD written = 0;
      WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
    }
  }
  CloseHandle(file);
}

inline void AppendDeployerDiagnosticText(const wchar_t* stage,
                                         const wchar_t* value) noexcept {
  DeployerDiagnosticLastErrorGuard last_error_guard;
  WCHAR message[1024] = {};
  if (SUCCEEDED(StringCchPrintfW(message, _countof(message), L"%s: %s", stage,
                                 value ? value : L""))) {
    AppendDeployerDiagnostic(message);
  }
}

inline void AppendDeployerDiagnosticValue(const wchar_t* stage,
                                          int value) noexcept {
  DeployerDiagnosticLastErrorGuard last_error_guard;
  WCHAR message[512] = {};
  if (SUCCEEDED(StringCchPrintfW(message, _countof(message), L"%s=%d", stage,
                                 value))) {
    AppendDeployerDiagnostic(message);
  }
}
