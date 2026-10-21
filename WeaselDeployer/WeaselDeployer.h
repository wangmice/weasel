#pragma once

#include "resource.h"
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
  HANDLE cancel_event_ = nullptr;
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
