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

/* Build a composing context with count candidates and numbered labels.
 * candies/labels/comments must be the same length: the panel reads them
 * pairwise while drawing, exactly like the vectors rime serializes. */
static weasel::Context make_candidate_ctx(int count, int highlighted) {
  weasel::Context ctx;
  for (int i = 0; i < count; ++i) {
    weasel::Text cand;
    cand.str = L"candidate";
    ctx.cinfo.candies.push_back(cand);
    weasel::Text label;
    label.str = std::to_wstring(i + 1);
    ctx.cinfo.labels.push_back(label);
    weasel::Text comment;
    ctx.cinfo.comments.push_back(comment);
  }
  ctx.cinfo.highlighted = highlighted;
  return ctx;
}

/* B9: highlighted may arrive out of range (unvalidated IPC data or a host
 * writing the context directly); refresh must clamp it into [0, count) so
 * downstream fixed-size array indexing (_candidateRects, m_offsetys) stays
 * in bounds, while in-range selections pass through unchanged */
static void test_highlighted_clamped(UiThread& t) {
  weasel::Status status;
  status.composing = true;

  t.ui.Update(make_candidate_ctx(3, 1), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 1; }, 3000),
        "B9: in-range highlighted preserved");

  t.ui.Update(make_candidate_ctx(3, 999), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 0; }, 3000),
        "B9: out-of-range highlighted clamped to 0");

  t.ui.Update(make_candidate_ctx(3, 1), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 1; }, 3000),
        "B9: highlighted recovered after clamp");

  t.ui.Update(make_candidate_ctx(3, -7), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 0; }, 3000),
        "B9: negative highlighted clamped to 0");
}

/* B8: HR() must tolerate S_FALSE (a success code) and still throw on real
 * failures — it used to throw on anything != S_OK */
static void test_hr_tolerates_s_false() {
  bool threw = false;
  try {
    HR(S_FALSE);
  } catch (const ComException&) {
    threw = true;
  }
  check(!threw, "B8: HR(S_FALSE) does not throw");

  threw = false;
  try {
    HR(E_FAIL);
  } catch (const ComException&) {
    threw = true;
  }
  check(threw, "B8: HR(E_FAIL) throws ComException");
}

/* B8: a style whose layout measurements fail in DirectWrite (negative
 * max_width makes CreateTextLayout return E_INVALIDARG, so GetTextSizeDW
 * throws via HR) must not kill the process: the exception has to be
 * shielded inside the panel, and once a sane style arrives the panel must
 * recover and keep updating */
static void test_layout_failure_is_shielded(UiThread& t) {
  weasel::UIStyle bad;
  bad.font_face = L"Segoe UI";
  bad.font_point = 12;
  bad.max_width = -100;  // CreateTextLayout(width<0) -> E_INVALIDARG

  weasel::Status status;
  status.composing = true;

  // 失败样式 + 有效候选：布局抛异常，必须被绘制路径内的 catch 挡住
  // （否则异常穿 WNDPROC，UI 线程终止，整个测试进程崩溃）
  t.ui.SetStyle(bad);
  t.ui.Update(make_candidate_ctx(3, 2), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 2; }, 3000),
        "B8: failing layout is shielded, process alive");

  // 恢复正常样式：面板应继续工作
  weasel::UIStyle good;
  good.font_face = L"Segoe UI";
  good.font_point = 12;
  t.ui.SetStyle(good);
  t.ui.Update(make_candidate_ctx(3, 1), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 1; }, 3000),
        "B8: panel recovers after layout failures");
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
  test_highlighted_clamped(t);
  test_hr_tolerates_s_false();
  test_layout_failure_is_shielded(t);

  t.RequestStop();
  check(ui_thread.timed_join(boost::posix_time::seconds(5)),
        "B1: UI thread exits cleanly");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  std::cout.flush();
  return g_failures ? 1 : 0;
}
