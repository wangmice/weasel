#include <windows.h>

#include "../../WeaselServer/SystemTraySDK.h"

#include <deque>
#include <iostream>

namespace {
int g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cout << "[FAIL] " << what << std::endl;
  } else {
    std::cout << "[ok] " << what << std::endl;
  }
}

class FakeTray final : public CSystemTray {
 public:
  ~FakeTray() override {
    // Keep the base destructor from issuing a real shell call or trying to
    // destroy the synthetic HWND used only for WindowProc routing.
    m_bRemoved = TRUE;
    m_hWnd = nullptr;
  }

  void PrepareVisible() {
    m_bEnabled = TRUE;
    m_bHidden = FALSE;
    m_bRemoved = FALSE;
    m_bShowIconPending = FALSE;
    m_uCreationFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_hWnd = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x1234));
  }

  void PrepareRemoved() {
    PrepareVisible();
    m_bHidden = TRUE;
    m_bRemoved = TRUE;
  }

  void PreparePendingAfterFailedAdd() {
    PrepareVisible();
    m_bHidden = TRUE;
    m_bRemoved = TRUE;
    m_bShowIconPending = TRUE;
  }

  void QueueResult(BOOL result) { results_.push_back(result); }

  bool Pending() const { return m_bShowIconPending != FALSE; }
  bool Hidden() const { return m_bHidden != FALSE; }
  bool Removed() const { return m_bRemoved != FALSE; }
  int AddCalls() const { return add_calls_; }
  UINT Flags() const { return m_tnd.uFlags; }
  HWND FakeWindow() const { return m_hWnd; }

 protected:
  BOOL NotifyIcon(DWORD message) override {
    if (message == NIM_ADD)
      ++add_calls_;
    if (results_.empty())
      return TRUE;
    const BOOL result = results_.front();
    results_.pop_front();
    return result;
  }

 private:
  int add_calls_ = 0;
  std::deque<BOOL> results_;
};

void test_taskbar_recreation_readds_visible_icon() {
  FakeTray tray;
  tray.PrepareVisible();
  tray.QueueResult(TRUE);

  tray.OnTaskbarCreated(0, 0);

  check(tray.AddCalls() == 1,
        "TaskbarCreated re-adds an icon that was visible before Explorer reset");
  check(!tray.Pending(), "successful taskbar recovery clears pending state");
  check(!tray.Hidden() && !tray.Removed(),
        "successful taskbar recovery keeps visible shell state");
  check(tray.Flags() == (NIF_MESSAGE | NIF_ICON | NIF_TIP),
        "taskbar recovery restores creation flags before NIM_ADD");
}

void test_taskbar_recreation_does_not_revive_removed_icon() {
  FakeTray tray;
  tray.PrepareRemoved();

  tray.OnTaskbarCreated(0, 0);

  check(tray.AddCalls() == 0,
        "TaskbarCreated does not revive an intentionally removed icon");
  check(!tray.Pending(), "removed icon does not become pending after restart");
}

void test_pending_hidden_failed_add_is_retried() {
  FakeTray tray;
  tray.PreparePendingAfterFailedAdd();
  tray.QueueResult(TRUE);

  tray.OnTaskbarCreated(0, 0);

  check(tray.AddCalls() == 1,
        "pending icon is retried even when a prior failed add marked it hidden");
  check(!tray.Pending(), "successful pending retry clears pending state");
  check(!tray.Hidden() && !tray.Removed(),
        "successful pending retry restores installed shell state");
}

void test_failed_recovery_stays_pending_and_retries() {
  FakeTray tray;
  tray.PrepareVisible();
  tray.QueueResult(FALSE);
  tray.QueueResult(TRUE);

  tray.OnTaskbarCreated(0, 0);
  check(tray.AddCalls() == 1, "first taskbar recovery attempt reaches NIM_ADD");
  check(tray.Pending(), "failed taskbar recovery remains pending");
  check(!tray.Hidden() && !tray.Removed(),
        "failed recovery does not turn a desired-visible icon into hidden state");

  tray.OnTaskbarCreated(0, 0);
  check(tray.AddCalls() == 2, "later taskbar recreation retries pending recovery");
  check(!tray.Pending(), "successful retry clears pending recovery state");
}

void test_setting_change_dispatch_retries_pending_install() {
  FakeTray tray;
  tray.PrepareVisible();
  tray.QueueResult(FALSE);
  tray.OnTaskbarCreated(0, 0);
  check(tray.Pending(), "precondition: recovery is pending after failed add");

  tray.QueueResult(TRUE);
  CSystemTray::WindowProc(tray.FakeWindow(), WM_SETTINGCHANGE, SPI_SETWORKAREA,
                          0);

  check(tray.AddCalls() == 2,
        "WM_SETTINGCHANGE/SPI_SETWORKAREA reaches pending tray recovery");
  check(!tray.Pending(), "setting-change retry clears pending recovery");
}
}  // namespace

int main() {
  test_taskbar_recreation_readds_visible_icon();
  test_taskbar_recreation_does_not_revive_removed_icon();
  test_pending_hidden_failed_add_is_retried();
  test_failed_recovery_stays_pending_and_retries();
  test_setting_change_dispatch_retries_pending_install();

  std::cout << (g_failures ? "FAILED" : "PASSED") << ": " << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
