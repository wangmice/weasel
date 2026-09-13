#include "stdafx.h"
#include "WeaselTrayIcon.h"
#include <atlstr.h>

// nasty
#include <resource.h>

static UINT mode_icon[] = {IDI_ZH, IDI_ZH, IDI_EN, IDI_RELOAD};
static const WCHAR* mode_label[] = {NULL, /*L"中文"*/ NULL, /*L"西文"*/ NULL,
                                    L"Under maintenance"};

WeaselTrayIcon::WeaselTrayIcon(weasel::UI& ui)
    : m_style(ui.style()),
      m_status(ui.status()),
      m_mode(INITIAL),
      m_schema_zhung_icon(),
      m_schema_ascii_icon(),
      m_disabled(false) {}

void WeaselTrayIcon::CustomizeMenu(HMENU hMenu) {}

BOOL WeaselTrayIcon::Create(HWND hTargetWnd) {
  HMODULE hModule = GetModuleHandle(NULL);
  CIcon icon;
  icon.LoadIconW(IDI_ZH);
  BOOL bRet =
      CSystemTray::Create(hModule, NULL, WM_WEASEL_TRAY_NOTIFY,
                          get_weasel_ime_name().c_str(), icon, IDR_MENU_POPUP);
  if (hTargetWnd) {
    SetTargetWnd(hTargetWnd);
  }
  if (!m_style.display_tray_icon) {
    RemoveIcon();
  } else {
    AddIcon();
  }
  return bRet;
}

void WeaselTrayIcon::RequestRefresh() {
  // 只投递请求，不读取 m_style/m_status：调用方是管道工作线程，而这两个引用
  // 绑定的 UI 状态可能正被 UI 线程赋值（含 std::wstring），跨线程读取是数据
  // 竞争。快照由 ApplyRefresh 在服务端消息线程上执行。
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_refresh_enabled) {
    return;
  }
  if (m_refresh_pending) {
    return;
  }
  m_refresh_pending = true;
  if (!::PostMessage(GetTargetWnd(), WM_WEASEL_SERVICE_NOTIFY, 0, 0)) {
    m_refresh_pending = false;
  }
}

void WeaselTrayIcon::ApplyRefresh() {
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_refresh_pending || !m_refresh_enabled) {
      return;
    }
    m_refresh_pending = false;
    m_refresh_in_progress = true;
  }
  // 服务端消息线程：与 OnApplyStyle/_ApplyUpdate 对 style_/status_ 的写入
  // 同线程，此处读取无竞争；投递期间到达的多次请求在此合并为一次最新快照
  Refresh(WeaselTrayIconState::From(m_style, m_status));
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_refresh_in_progress = false;
  }
  m_state_cv.notify_all();
}

void WeaselTrayIcon::DisableRefresh() {
  std::unique_lock<std::mutex> lock(m_state_mutex);
  m_refresh_enabled = false;
  m_refresh_pending = false;
  m_state_cv.wait(lock, [this] { return !m_refresh_in_progress; });
}

void WeaselTrayIcon::Refresh(const WeaselTrayIconState& state) {
  if (!state.display_tray_icon &&
      !state.disabled)  // display notification when deploying
  {
    if (m_mode != INITIAL) {
      RemoveIcon();
      m_mode = INITIAL;
    }
    m_disabled = false;
    return;
  }
  WeaselTrayMode mode = state.disabled     ? DISABLED
                        : state.ascii_mode ? ASCII
                                           : ZHUNG;
  /* change icon, when
          1,mode changed
          2,icon changed
          3,both m_schema_zhung_icon and state.current_zhung_icon empty(for
     initialize) 4,both m_schema_ascii_icon and state.current_ascii_icon
     empty(for initialize)
  */
  if (mode != m_mode || m_schema_zhung_icon != state.current_zhung_icon ||
      (m_schema_zhung_icon.empty() && state.current_zhung_icon.empty()) ||
      m_schema_ascii_icon != state.current_ascii_icon ||
      (m_schema_ascii_icon.empty() && state.current_ascii_icon.empty())) {
    ShowIcon();
    m_mode = mode;
    m_schema_zhung_icon = state.current_zhung_icon;
    m_schema_ascii_icon = state.current_ascii_icon;
    if (mode == ASCII) {
      if (m_schema_ascii_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_ascii_icon.c_str());
    } else if (mode == ZHUNG) {
      if (m_schema_zhung_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_zhung_icon.c_str());
    } else
      SetIcon(mode_icon[mode]);

    if (mode_label[mode] && m_disabled == false) {
      CString info;
      info.LoadStringW(IDS_STR_UNDER_MAINTENANCE);
      ShowBalloon(info, get_weasel_ime_name().c_str());
      m_disabled = true;
    }
    if (m_mode != DISABLED)
      m_disabled = false;
  } else if (!Visible()) {
    ShowIcon();
  }
}
