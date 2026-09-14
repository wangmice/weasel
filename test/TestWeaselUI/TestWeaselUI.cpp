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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

static int g_failures = 0;

/* B23: counted via _set_invalid_parameter_handler. The safe formatting path
 * (_snwprintf_s + _TRUNCATE) must never raise it; the handler makes a
 * regression (back to swprintf_s) fail the test loudly instead of killing
 * the debug process with the "Buffer too small" assertion (repro_b23). */
static int g_invalid_parameter_fired = 0;

static void count_invalid_parameter(const wchar_t* /*expression*/,
                                    const wchar_t* /*function*/,
                                    const wchar_t* /*file*/,
                                    unsigned int /*line*/,
                                    uintptr_t /*reserved*/) {
  ++g_invalid_parameter_fired;
}

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
 * candies/labels/comments may legitimately differ in length: rime always
 * serializes them pairwise, but IPC deserialization or a host writing the
 * context directly carries no such guarantee (B37). */
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

/* B37: candies longer than labels/comments must render as empty strings at
 * the missing indices instead of throwing out_of_range from GetLabelText
 * (layout) and comments.at() (paint). The panel's B8 shielding keeps the
 * process alive either way, so the observable is whether a full
 * layout+paint cycle succeeds: m_octx only settles equal to the landed
 * context when no exception keeps triggering _RequestPaintRecovery. */
static weasel::Context make_mismatched_ctx(int candies,
                                           int labels,
                                           int comments) {
  weasel::Context ctx;
  auto push = [](std::vector<weasel::Text>& v, const wchar_t* s) {
    weasel::Text t;
    t.str = s;
    v.push_back(t);
  };
  for (int i = 0; i < candies; ++i)
    push(ctx.cinfo.candies, L"candidate");
  for (int i = 0; i < labels; ++i)
    push(ctx.cinfo.labels, std::to_wstring(i + 1).c_str());
  for (int i = 0; i < comments; ++i)
    push(ctx.cinfo.comments, L"comment");
  ctx.cinfo.highlighted = 0;
  return ctx;
}

static void test_mismatched_candidate_vectors(UiThread& t) {
  // 5 个候选 / 1 个标签 / 0 条注释：此前两处 at() 越界各会抛一次
  // out_of_range（布局期 GetLabelText 与绘制期 comments.at）
  weasel::Status status;
  status.composing = true;

  t.ui.Update(make_mismatched_ctx(5, 1, 0), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.candies.size() == 5; }, 3000),
        "B37: mismatched context lands");
  // 布局 + 绘制全链路成功时 m_octx 才会与 m_ctx 一致并保持；仍抛异常则
  // _RequestPaintRecovery 会不断清空 m_octx（旧值 3 来自前一个用例）
  check(wait_until([&] { return t.ui.octx().cinfo.candies.size() == 5; },
                   3000),
        "B37: layout+paint completes with short labels/comments");
  Sleep(500);  // 让恢复重投（若有）跑完后再确认不回退
  check(t.ui.octx().cinfo.candies.size() == 5,
        "B37: paint result stays settled");

  // 恢复等长向量后一切如常
  t.ui.Update(make_candidate_ctx(3, 1), status);
  check(wait_until([&] { return t.ui.ctx().cinfo.highlighted == 1; }, 3000),
        "B37: equal-length context still works after mismatch");
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

/* B35-1: with exactly MAX_CANDIDATES_COUNT (100) candidates, the row/column
 * round-info passes must not read the [i+1] neighbor past the array capacity:
 * row_of_candidate[i+1] / col_of_candidate[i+1] used to read index [100] when
 * i was the last candidate and count hit the 100 cap (arrays are
 * int[MAX_CANDIDATES_COUNT]). A settled octx proves the layout+paint cycle
 * completed. Styles below force the affected paths: horizontal wrapping into
 * multiple rows (HorizontalLayout round-info loop) and vertical text with
 * column wrap in both directions (VHorizontalLayout::DoLayoutWithWrap). */
static void test_max_candidates_round_info(UiThread& t) {
  weasel::Status status;
  status.composing = true;

  // 横排 + 窄 max_width：候选换行成多行，row_cnt > 0 进入圆角调整循环
  weasel::UIStyle horizontal;
  horizontal.font_face = L"Segoe UI";
  horizontal.font_point = 12;
  horizontal.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
  horizontal.max_width = 120;
  t.ui.SetStyle(horizontal);
  t.ui.Update(make_candidate_ctx(100, 0), status);
  check(wait_until([&] { return t.ui.octx().cinfo.candies.size() == 100; },
                   3000),
        "B35-1: 100 candidates settle under horizontal multi-row layout");

  // 竖排文本 + 自动换列，两个方向各走一遍 DoLayoutWithWrap 的列圆角循环
  weasel::UIStyle vtext_wrap;
  vtext_wrap.font_face = L"Segoe UI";
  vtext_wrap.font_point = 12;
  vtext_wrap.layout_type = weasel::UIStyle::LAYOUT_VERTICAL_TEXT;
  vtext_wrap.vertical_text_with_wrap = true;
  vtext_wrap.max_height = 120;

  vtext_wrap.vertical_text_left_to_right = true;
  t.ui.SetStyle(vtext_wrap);
  t.ui.Update(make_candidate_ctx(100, 1), status);
  check(wait_until([&] { return t.ui.octx().cinfo.highlighted == 1; }, 3000),
        "B35-1: 100 candidates settle under vertical-text wrap (l2r)");

  vtext_wrap.vertical_text_left_to_right = false;
  t.ui.SetStyle(vtext_wrap);
  t.ui.Update(make_candidate_ctx(100, 0), status);
  check(wait_until([&] { return t.ui.octx().cinfo.highlighted == 0; }, 3000),
        "B35-1: 100 candidates settle under vertical-text wrap (r2l)");
}

/* B23: label_text_format comes from user yaml (style/label_format) and the
 * formatted label itself can exceed the 128-wchar buffer (long custom labels).
 * Both must degrade gracefully: truncation for the overflow case (CRT asserts
 * and terminates the process with plain swprintf_s, repro_b23), fallback to
 * the default "%s." for format strings that do not match the single string
 * argument (%d, %n, double %s, trailing lone %). */
static void test_label_text_format_bounded() {
  check(FormatLabelText(L"%s.", L"1") == L"1.", "B23: default %s. format");
  check(FormatLabelText(L"%s", L"abc") == L"abc", "B23: bare %s");
  check(FormatLabelText(L"(%s)", L"5") == L"(5)", "B23: literals around %s");
  check(FormatLabelText(L"%%", L"x") == L"%", "B23: %% escapes to a literal %");
  check(FormatLabelText(L"", L"x").empty(), "B23: empty format is allowed");

  // 超长 label（repro_b23 的 140 字符场景）：截断到 127 个 wchar，进程存活
  std::wstring long_label(140, L'x');
  std::wstring formatted = FormatLabelText(L"%s.", long_label);
  check(formatted.size() == 127 && formatted == std::wstring(127, L'x'),
        "B23: 140-char label truncated to 127 wchars, no CRT termination");

  // 与唯一字符串参数不匹配的格式串：回退默认 %s.
  check(FormatLabelText(L"%d", L"7") == L"7.", "B23: %d falls back to %s.");
  check(FormatLabelText(L"%n", L"7") == L"7.", "B23: %n falls back to %s.");
  check(FormatLabelText(L"%ls", L"7") == L"7.", "B23: %ls falls back to %s.");
  check(FormatLabelText(L"abc%", L"7") == L"7.", "B23: trailing % falls back");
  check(FormatLabelText(L"%s%s", L"7") == L"7.", "B23: double %s falls back");
  check(FormatLabelText(nullptr, L"7") == L"7.", "B23: null format falls back");

  check(g_invalid_parameter_fired == 0,
        "B23: formatting never raises the CRT invalid parameter");
}

int main() {
  _set_invalid_parameter_handler(count_invalid_parameter);
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
  test_mismatched_candidate_vectors(t);
  test_max_candidates_round_info(t);
  test_label_text_format_bounded();

  t.RequestStop();
  check(ui_thread.timed_join(boost::posix_time::seconds(5)),
        "B1: UI thread exits cleanly");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  std::cout.flush();
  return g_failures ? 1 : 0;
}
