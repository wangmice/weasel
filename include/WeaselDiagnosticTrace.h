#pragma once
// Opt-in diagnostic breadcrumbs for startup/IPC hangs. Never log key data,
// schema contents, window titles or user paths. One append-only file per PID.
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cwchar>

namespace weasel::diagnostics {

inline bool Enabled() noexcept {
  static const bool enabled = []() noexcept {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel",
                        L"DiagnosticTrace", RRF_RT_REG_DWORD, nullptr,
                        &value, &size) == ERROR_SUCCESS &&
           value == 1;
  }();
  return enabled;
}

inline void Trace(const char* stage,
                  const char* event,
                  DWORD value = 0,
                  DWORD extra = 0) noexcept {
  if (!Enabled())
    return;

  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, path);
  if (length == 0 || length > MAX_PATH - 100)
    return;
  if (wcscat_s(path, L"rime.weasel") != 0)
    return;
  CreateDirectoryW(path, nullptr);
  if (wcscat_s(path, L"\\diagnostics") != 0)
    return;
  CreateDirectoryW(path, nullptr);

  wchar_t suffix[64] = {};
  _snwprintf_s(suffix, _countof(suffix), _TRUNCATE,
               L"\\weasel-diagnostic-%lu.log", GetCurrentProcessId());
  if (wcscat_s(path, suffix) != 0)
    return;

  const HANDLE file = CreateFileW(
      path, FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;

  SYSTEMTIME time{};
  GetLocalTime(&time);
  char line[384] = {};
  const int written = _snprintf_s(
      line, _countof(line), _TRUNCATE,
      "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu "
      "tick=%llu [WEASEL-DIAG] %s %s value=%lu extra=%lu\r\n",
      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
      time.wSecond, time.wMilliseconds, GetCurrentProcessId(),
      GetCurrentThreadId(), static_cast<unsigned long long>(GetTickCount64()),
      stage, event, value, extra);
  if (written > 0) {
    DWORD ignored = 0;
    WriteFile(file, line, static_cast<DWORD>(written), &ignored, nullptr);
  }
  CloseHandle(file);
}

// Globally throttled per process to avoid a filesystem write per failed key.
inline void TraceRateLimited(const char* stage,
                             const char* event,
                             DWORD value = 0,
                             DWORD extra = 0) noexcept {
  if (!Enabled())
    return;
  static std::atomic<ULONGLONG> previous{0};
  const ULONGLONG now = GetTickCount64();
  ULONGLONG observed = previous.load(std::memory_order_relaxed);
  if (observed != 0 && now - observed < 2000)
    return;
  if (previous.compare_exchange_strong(observed, now,
                                       std::memory_order_relaxed))
    Trace(stage, event, value, extra);
}

class Scope {
 public:
  explicit Scope(const char* stage) noexcept
      : stage_(stage), begin_(GetTickCount64()), enabled_(Enabled()) {
    if (enabled_)
      Trace(stage_, "begin");
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  ~Scope() {
    if (enabled_)
      Trace(stage_, "end",
            static_cast<DWORD>(GetTickCount64() - begin_));
  }

 private:
  const char* stage_;
  ULONGLONG begin_;
  bool enabled_;
};
}  // namespace weasel::diagnostics
