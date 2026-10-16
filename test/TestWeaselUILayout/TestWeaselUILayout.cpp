// UI window lifecycle and layout regression tests. Graphics objects are
// created and released on this COM-initialized thread, including shared devices.
#include <windows.h>
#include <WeaselUI.h>
#include "../../WeaselUI/Layout.h"
#include "../../WeaselUI/d2d.h"
#include "../../WeaselUI/StandardLayout.h"
#include "../../WeaselUI/VerticalLayout.h"
#include <atomic>
#include <cstdio>
#include <type_traits>

#define private public
#include "../../WeaselUI/WeaselPanel.h"
#include "../../WeaselUI/FullScreenLayout.h"
#undef private

static int g_failures = 0;

static void check(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

class TrackedLayout : public weasel::VerticalLayout {
 public:
  TrackedLayout(const weasel::UIStyle& style,
                const weasel::Context& ctx,
                const weasel::Status& status,
                weasel::an<weasel::D2D>& d2d,
                bool& destroyed)
      : VerticalLayout(style, ctx, status, d2d), m_destroyed(destroyed) {}
  ~TrackedLayout() { m_destroyed = true; }

 private:
  bool& m_destroyed;
};

static void test_polymorphic_layout_destruction() {
  const bool safe = std::has_virtual_destructor<weasel::Layout>::value;
  check(safe, "Layout supports safe destruction through a base pointer");
  if (!safe)
    return;  // Do not invoke undefined behavior in a non-sanitized test.
  weasel::UI ui;
  ui.style().font_face = L"Segoe UI";
  ui.style().font_point = 12;
  weasel::WeaselPanel panel(ui);
  bool created = panel.Create(nullptr);
  check(created, "polymorphic destruction test creates graphics resources");
  if (!created)
    return;
  weasel::Context ctx;
  weasel::Status status;
  bool destroyed = false;
  std::unique_ptr<weasel::Layout> layout = std::make_unique<TrackedLayout>(
      ui.style(), ctx, status, panel.m_pD2D, destroyed);
  layout.reset();
  check(destroyed, "base unique_ptr invokes the derived layout destructor");

  destroyed = false;
  RECT input_pos{0, 0, 1, 1};
  auto inner = std::make_unique<TrackedLayout>(
      ui.style(), ctx, status, panel.m_pD2D, destroyed);
  layout = std::make_unique<weasel::FullScreenLayout>(
      ui.style(), ctx, status, input_pos, std::move(inner), panel.m_pD2D);
  layout.reset();
  check(destroyed, "base destruction releases the nested fullscreen layout");
  panel.DestroyWindow();
}

static void test_recreate_with_unchanged_context() {
  weasel::UI ui;
  ui.style().font_face = L"Segoe UI";
  ui.style().font_point = 12;
  weasel::WeaselPanel panel(ui);
  weasel::Context ctx;
  ctx.preedit.str = L"a";
  ctx.cinfo.candies.emplace_back(L"candidate");
  ctx.cinfo.labels.emplace_back(L"1");
  weasel::Status status;
  status.composing = true;

  bool created = panel.Create(nullptr);
  check(created, "initial candidate window is created");
  if (!created)
    return;
  panel.ApplyUpdate(ctx, status);
  check(panel.m_layout_ready && panel.m_layout && ui.octx() == ui.ctx(),
        "initial candidate layout and paint complete");

  for (int cycle = 0; cycle < 2; ++cycle) {
    panel.DestroyWindow();
    check(!panel.m_layout, "destroying the window releases its layout");
    // Clear the last-painted snapshot so a stale successful frame cannot
    // satisfy the rendering assertion on the newly created window.
    ui.octx() = weasel::Context();
    created = panel.Create(nullptr);
    check(created, "candidate window can be recreated");
    if (!created)
      return;
    check(panel.m_layout_ready && panel.m_layout,
          "recreation rebuilds the layout before any new response");
    panel.ApplyUpdate(ctx, status);
    panel.ShowWindow(SW_SHOWNA);
    UpdateWindow(panel.hwnd());
    check(panel.IsShown() && panel.m_layout_ready && panel.m_layout &&
              ui.octx() == ui.ctx(),
          "unchanged response paints the recreated window without Refresh");
  }

  auto* layout = panel.m_layout.get();
  check(panel.Create(nullptr) && panel.m_layout.get() == layout,
        "Create on an existing window preserves its ready layout");

  // Model lost layout state independently of HWND creation. Deduplication
  // must not discard the only response that can restore it.
  panel.m_layout.reset();
  panel.m_layout_ready = false;
  ui.octx() = weasel::Context();
  panel.ApplyUpdate(ctx, status);
  check(panel.m_layout_ready && panel.m_layout && ui.octx() == ui.ctx(),
        "unchanged response restores a missing layout");
  panel.DestroyWindow();
}

static BOOL CALLBACK collect_work_areas(HMONITOR monitor,
                                        HDC,
                                        LPRECT,
                                        LPARAM data) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfo(monitor, &info))
    reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);
  return TRUE;
}

static void test_fullscreen_tracks_input_rectangle() {
  weasel::UI ui;
  ui.style().font_face = L"Segoe UI";
  ui.style().font_point = 12;
  weasel::WeaselPanel panel(ui);
  bool created = panel.Create(nullptr);
  check(created, "fullscreen test creates graphics resources");
  if (!created)
    return;

  weasel::Context ctx;
  ctx.preedit.str = L"a";
  ctx.cinfo.candies.emplace_back(L"candidate");
  ctx.cinfo.labels.emplace_back(L"1");
  weasel::Status status;
  status.composing = true;
  weasel::UIStyle style = ui.style();
  style.layout_type = weasel::UIStyle::LAYOUT_VERTICAL_FULLSCREEN;
  RECT input_pos{0, 0, 1, 1};
  auto inner = std::make_unique<weasel::VerticalLayout>(
      style, ctx, status, panel.m_pD2D);
  weasel::FullScreenLayout layout(style, ctx, status, input_pos,
                                 std::move(inner), panel.m_pD2D);
  // Compare addresses before reading the stored reference: the old CRect
  // conversion creates a dead temporary, which must not be dereferenced.
  bool aliases_input = static_cast<const RECT*>(&layout.mr_inputPos) ==
                       &input_pos;
  check(aliases_input, "fullscreen retains the caller's RECT, not a temporary");
  if (aliases_input) {
    std::vector<RECT> work_areas;
    EnumDisplayMonitors(nullptr, nullptr, collect_work_areas,
                        reinterpret_cast<LPARAM>(&work_areas));
    check(!work_areas.empty(), "fullscreen test finds a monitor work area");
    for (const RECT& area : work_areas) {
      input_pos = {(area.left + area.right) / 2,
                   (area.top + area.bottom) / 2,
                   (area.left + area.right) / 2 + 1,
                   (area.top + area.bottom) / 2 + 1};
      check(EqualRect(&layout.mr_inputPos, &input_pos),
            "fullscreen observes input position changes after construction");
      layout.DoLayout();
      const CSize& size = layout.GetContentSize();
      check(size.cx == area.right - area.left &&
                size.cy == area.bottom - area.top,
            "fullscreen layout uses the current input monitor's work area");
    }
    std::printf("Monitor work areas tested: %zu\n", work_areas.size());
  }
  panel.DestroyWindow();
}

int main() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    std::printf("COM initialization failed: 0x%08lx\n", hr);
    return 1;
  }
  test_polymorphic_layout_destruction();
  test_recreate_with_unchanged_context();
  test_fullscreen_tracks_input_rectangle();
  // DeviceResources now keeps only a weak reference to the published
  // generation. Once all panel/D2D owners above have unwound, no shared
  // generation should remain alive; verify that before tearing down COM.
  check(!weasel::DeviceResources::Get().HasLiveGeneration(),
        "UI tests release the shared graphics generation before COM teardown");
  CoUninitialize();
  std::printf("Failures: %d\n", g_failures);
  return g_failures ? 1 : 0;
}
