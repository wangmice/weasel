// TestWeaselUI.cpp : UI thread marshal tests for the weasel UI layer.
//
// B1 regression: Show/Hide/ShowWithTimeout (and the countdown-cancel in
// Update) must marshal back to the window-owning thread via WM_WEASEL_SHOW.
// The calling thread below plays the role of an IPC worker thread; a
// dedicated UI thread owns the panel window and pumps messages.

#include <windows.h>

#include <WeaselUI.h>

#include <boost/thread.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cout << "[FAIL] " << what << std::endl;
  } else {
    std::cout << "[ok] " << what << std::endl;
  }
}

/* Poll pred until it holds or the timeout elapses (marshaled operations
 * complete asynchronously once the UI thread pumps the posted message). */
template <typename Pred>
static bool wait_until(Pred pred, int timeout_ms, int step_ms = 10) {
  for (int waited = 0; waited < timeout_ms; waited += step_ms) {
    if (pred())
      return true;
    Sleep(step_ms);
  }
  return pred();
}

/* UI thread: owns the panel window, runs the message pump, and verifies the
 * same-thread direct-call path (must take effect without any pumping). */
class UiThread {
 public:
  UiThread() {
    m_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
  }
  ~UiThread() {
    if (m_ready)
      CloseHandle(m_ready);
    if (m_stop)
      CloseHandle(m_stop);
  }

  weasel::UI ui;
  bool created = false;
  bool same_thread_show_ok = false;
  bool same_thread_hide_ok = false;

  void Run() {
    created = ui.Create(NULL);
    if (created) {
      // 同线程直调：不经消息泵立即生效
      ui.Show();
      same_thread_show_ok = ui.IsShown();
      ui.Hide();
      same_thread_hide_ok = !ui.IsShown();
    }
    SetEvent(m_ready);
    // pump until asked to stop
    while (WaitForSingleObject(m_stop, 0) != WAIT_OBJECT_0) {
      MSG msg;
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      MsgWaitForMultipleObjects(1, &m_stop, FALSE, 50, QS_ALLINPUT);
    }
    // destroy the window on its owning thread
    ui.Destroy(true);
  }

  void NotifyReady() { WaitForSingleObject(m_ready, INFINITE); }
  void RequestStop() { SetEvent(m_stop); }

 private:
  HANDLE m_ready = NULL;
  HANDLE m_stop = NULL;
};

/* B1: a non-UI thread calling Show/Hide must observe the state flip once the
 * UI thread processes the posted command (no direct cross-thread ShowWindow) */
static void test_cross_thread_show_hide(UiThread& t) {
  t.ui.Show();
  check(wait_until([&] { return t.ui.IsShown(); }, 3000),
        "B1: cross-thread Show takes effect on UI thread");

  t.ui.Hide();
  check(wait_until([&] { return !t.ui.IsShown(); }, 3000),
        "B1: cross-thread Hide takes effect on UI thread");
}

/* B1: ShowWithTimeout must show the window, expose the countdown, and
 * auto-hide when the timer fires (WM_TIMER path on the UI thread) */
static void test_show_with_timeout(UiThread& t) {
  t.ui.ShowWithTimeout(200);
  check(wait_until([&] { return t.ui.IsShown() && t.ui.IsCountingDown(); },
                   3000),
        "B1: cross-thread ShowWithTimeout shows and counts down");
  check(wait_until([&] { return !t.ui.IsShown() && !t.ui.IsCountingDown(); },
                   3000),
        "B1: auto-hide timer fires on UI thread");
}

/* B1: the Hide + KillTimer prelude of Update (cancelling an in-progress
 * countdown) must run on the UI thread before the snapshot lands */
static void test_update_cancels_countdown(UiThread& t) {
  t.ui.ShowWithTimeout(60000);  // long enough that only Update ends it
  check(wait_until([&] { return t.ui.IsCountingDown(); }, 3000),
        "B1: countdown armed");

  weasel::Context ctx;  // empty snapshot: dedup would skip, cancel must not
  weasel::Status status;
  t.ui.Update(ctx, status);
  check(wait_until([&] { return !t.ui.IsCountingDown() && !t.ui.IsShown(); },
                   3000),
        "B1: Update cancels countdown and hides tip window");
}

int main() {
  UiThread t;
  boost::thread ui_thread([&t] { t.Run(); });
  t.NotifyReady();

  check(t.created, "B1: panel window created");
  check(t.same_thread_show_ok, "B1: same-thread Show is synchronous");
  check(t.same_thread_hide_ok, "B1: same-thread Hide is synchronous");

  test_cross_thread_show_hide(t);
  test_show_with_timeout(t);
  test_update_cancels_countdown(t);

  t.RequestStop();
  check(ui_thread.timed_join(boost::posix_time::seconds(5)),
        "B1: UI thread exits cleanly");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  std::cout.flush();
  return g_failures ? 1 : 0;
}
