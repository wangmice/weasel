#pragma once

#include "resource.h"
#include <atomic>
#include <string>
#include <atlstr.h>
#include <Windows.h>
#include "resource.h"

class RimeOperationWatchdog {
 public:
  RimeOperationWatchdog() noexcept = default;
  RimeOperationWatchdog(const RimeOperationWatchdog&) = delete;
  RimeOperationWatchdog& operator=(const RimeOperationWatchdog&) = delete;
  ~RimeOperationWatchdog();

  bool Arm() noexcept;
  void Disarm() noexcept;

 private:
  enum class Decision { Disarmed, Armed, Firing };
  static DWORD WINAPI ThreadProc(LPVOID context) noexcept;

  std::atomic<Decision> decision_{Decision::Disarmed};
  HANDLE cancel_event_ = nullptr;
  HANDLE thread_ = nullptr;
};

#define MSG_BY_IDS(idInfo, idCap, uType)           \
  {                                                \
    CString info, cap;                             \
    info.LoadStringW(idInfo);                      \
    cap.LoadStringW(idCap);                        \
    LANGID langID = GetThreadUILanguage();         \
    MessageBoxExW(NULL, info, cap, uType, langID); \
  }

#define MSG_ID_CAP(info, idCap, uType)             \
  {                                                \
    CString cap;                                   \
    cap.LoadStringW(idCap);                        \
    LANGID langID = GetThreadUILanguage();         \
    MessageBoxExW(NULL, info, cap, uType, langID); \
  }
