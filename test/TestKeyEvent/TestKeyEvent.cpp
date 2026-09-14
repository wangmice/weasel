// TestKeyEvent.cpp : unit tests for ConvertKeyEvent / KeyInfo packing (K3).
//
// Only layout-independent behavior is asserted: VK -> ibus keycode
// translation, modifier masks, and the KeyInfo bit layout that feeds
// ToUnicodeEx's scan-code argument. The ToUnicodeEx character fallback
// depends on the active keyboard layout, so it is only exercised
// through an unmapped reserved VK (must stay "unknown" on any layout).

#include <Windows.h>
#include <stdio.h>

#include "../../include/KeyEvent.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

// 按 WM_KEYDOWN/WM_KEYUP lParam 的位布局构造参数
static LPARAM mkLparam(UINT scan, bool ext, bool up, UINT repeat = 1) {
  LPARAM lp = repeat | (scan << 16);
  if (ext)
    lp |= (LPARAM)1 << 24;
  if (up)
    lp |= (LPARAM)1 << 31;
  return lp;
}

static BYTE nullState[256] = {};

static void testKeyInfoPacking() {
  KeyInfo down(mkLparam(0x1D, false, false));
  check(down.repeatCount == 1 && down.scanCode == 0x1D && !down.isExtended &&
            !down.isKeyUp,
        "packing: plain key down fields");

  KeyInfo extUp(mkLparam(0x1C, true, true, 3));
  check(extUp.repeatCount == 3 && extUp.scanCode == 0x1C && extUp.isExtended &&
            extUp.isKeyUp,
        "packing: extended key up fields");

  // UINT32 打包往返（K3：该打包值曾被整体误作扫描码传给 ToUnicodeEx）
  KeyInfo orig(mkLparam(0x2A, true, false, 2));
  UINT32 packed = orig;
  KeyInfo back = *reinterpret_cast<KeyInfo*>(&packed);
  check(back.repeatCount == 2 && back.scanCode == 0x2A && back.isExtended &&
            !back.isKeyUp,
        "packing: operator UINT32 round trip");
}

static void testTranslateKeycode() {
  weasel::KeyEvent ev;

  check(ConvertKeyEvent(VK_RETURN, mkLparam(0x1C, false, false), nullState,
                        ev) &&
            ev.keycode == ibus::Return && ev.mask == ibus::NULL_MASK,
        "translate: Enter -> Return, no mask");

  check(ConvertKeyEvent(VK_RETURN, mkLparam(0x1C, true, false), nullState, ev) &&
            ev.keycode == ibus::KP_Enter,
        "translate: extended Enter -> KP_Enter");

  check(ConvertKeyEvent(VK_SHIFT, mkLparam(0x2A, false, false), nullState, ev) &&
            ev.keycode == ibus::Shift_L,
        "translate: left shift scan 0x2A -> Shift_L");

  check(ConvertKeyEvent(VK_SHIFT, mkLparam(0x36, false, false), nullState, ev) &&
            ev.keycode == ibus::Shift_R,
        "translate: right shift scan 0x36 -> Shift_R");

  check(ConvertKeyEvent(VK_CONTROL, mkLparam(0x1D, false, false), nullState,
                        ev) &&
            ev.keycode == ibus::Control_L,
        "translate: Control -> Control_L");

  check(ConvertKeyEvent(VK_CONTROL, mkLparam(0x1D, true, false), nullState,
                        ev) &&
            ev.keycode == ibus::Control_R,
        "translate: extended Control -> Control_R");

  check(ConvertKeyEvent(VK_SPACE, mkLparam(0x39, false, false), nullState, ev) &&
            ev.keycode == ibus::space,
        "translate: Space -> space");

  check(ConvertKeyEvent(VK_ESCAPE, mkLparam(0x01, false, true), nullState, ev) &&
            ev.keycode == ibus::Escape &&
            (ev.mask & ibus::RELEASE_MASK),
        "translate: Esc up -> Escape + RELEASE_MASK");
}

static void testMasks() {
  weasel::KeyEvent ev;
  BYTE st[256] = {};

  st[VK_SHIFT] = 0x80;
  check(ConvertKeyEvent(VK_SPACE, mkLparam(0x39, false, false), st, ev) &&
            (ev.mask & ibus::SHIFT_MASK),
        "mask: shift down -> SHIFT_MASK");
  st[VK_SHIFT] = 0;

  st[VK_CONTROL] = 0x80;
  check(ConvertKeyEvent(VK_SPACE, mkLparam(0x39, false, false), st, ev) &&
            (ev.mask & ibus::CONTROL_MASK),
        "mask: ctrl down -> CONTROL_MASK");
  st[VK_CONTROL] = 0;

  st[VK_MENU] = 0x80;
  check(ConvertKeyEvent(VK_SPACE, mkLparam(0x39, false, false), st, ev) &&
            (ev.mask & ibus::ALT_MASK),
        "mask: alt down -> ALT_MASK");
  st[VK_MENU] = 0;

  // Caps：按下事件要先还原 LOCK_MASK（rime 期望先见事件后见状态翻转）
  st[VK_CAPITAL] = 0x01;  // 已锁定
  check(ConvertKeyEvent(VK_CAPITAL, mkLparam(0x3A, false, false), st, ev) &&
            ev.keycode == ibus::Caps_Lock && !(ev.mask & ibus::LOCK_MASK),
        "mask: caps down while locked -> LOCK_MASK reverted");

  check(ConvertKeyEvent(VK_CAPITAL, mkLparam(0x3A, false, false), nullState,
                        ev) &&
            (ev.mask & ibus::LOCK_MASK),
        "mask: caps down while unlocked -> LOCK_MASK set");

  check(ConvertKeyEvent(VK_CAPITAL, mkLparam(0x3A, false, true), st, ev) &&
            (ev.mask & ibus::LOCK_MASK) &&
            (ev.mask & ibus::RELEASE_MASK),
        "mask: caps up keeps LOCK_MASK (no revert on release)");
}

static void testUnmappedKey() {
  weasel::KeyEvent ev;
  // VK 0xE8 未分配：任何键盘布局下 ToUnicodeEx 都无翻译，应判为未知键。
  // 该路径同时覆盖 K3 的 ToUnicodeEx 调用（收真正的 8 位扫描码）不崩。
  check(!ConvertKeyEvent(0xE8, mkLparam(0xE8, false, false), nullState, ev) &&
            ev.keycode == 0,
        "unmapped: reserved VK -> unknown key, keycode 0");
}

int main() {
  testKeyInfoPacking();
  testTranslateKeycode();
  testMasks();
  testUnmappedKey();

  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
