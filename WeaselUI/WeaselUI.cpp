#include "stdafx.h"
#include <WeaselUI.h>
#include "WeaselPanel.h"

using namespace weasel;

class weasel::UIImpl {
 public:
  WeaselPanel panel;

  UIImpl(weasel::UI& ui) : panel(ui), shown(false) {}
  ~UIImpl() {}
  void Refresh() {
    if (!panel.IsWindow())
      return;
    if (timer) {
      Hide();
      KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
      timer = 0;
    }
    panel.Refresh();
  }
  // 快照内容是否变化由 panel 在 UI 线程上判断；这里只负责与旧 Update 路径
  // 一致的提示窗口清理，再经 panel 的 marshal 通道落地
  void Update(Context const& ctx, Status const& status) {
    if (timer) {
      Hide();
      KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
      timer = 0;
    }
    panel.ApplyUpdate(ctx, status);
  }
  void SetStyle(UIStyle const& style) { panel.ApplyStyle(style); }
  void Show();
  void Hide();
  void ShowWithTimeout(size_t millisec);
  bool IsShown() const { return shown; }

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOHIDE_TIMER = 20121220;
  static UINT_PTR timer;
  bool shown;
};

UINT_PTR UIImpl::timer = 0;

void UIImpl::Show() {
  if (!panel.IsWindow())
    return;
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::Hide() {
  if (!panel.IsWindow())
    return;
  panel.ShowWindow(SW_HIDE);
  shown = false;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::ShowWithTimeout(size_t millisec) {
  if (!panel.IsWindow())
    return;
  DLOG(INFO) << "ShowWithTimeout: " << millisec;
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
  SetTimer(panel.m_hWnd, AUTOHIDE_TIMER, static_cast<UINT>(millisec),
           &UIImpl::OnTimer);
  timer = UINT_PTR(this);
}
VOID CALLBACK UIImpl::OnTimer(_In_ HWND hwnd,
                              _In_ UINT uMsg,
                              _In_ UINT_PTR idEvent,
                              _In_ DWORD dwTime) {
  DLOG(INFO) << "OnTimer:";
  KillTimer(hwnd, idEvent);
  UIImpl* self = (UIImpl*)timer;
  timer = 0;
  if (self) {
    self->Hide();
    self->shown = false;
  }
}

bool UI::Create(HWND parent) {
  if (pimpl_) {
    pimpl_->panel.Create(
        parent, 0, 0, WS_POPUP,
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        0U, 0);
    return true;
  }

  pimpl_ = new UIImpl(*this);
  if (!pimpl_)
    return false;

  pimpl_->panel.Create(
      parent, 0, 0, WS_POPUP,
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      0U, 0);
  return true;
}

void UI::Destroy(bool full) {
  if (pimpl_) {
    // destroy panel
    if (pimpl_->panel.IsWindow()) {
      pimpl_->panel.DestroyWindow();
    }
    if (full) {
      delete pimpl_;
      pimpl_ = 0;
      pDWR.reset();
    }
  }
}

bool UI::GetIsReposition() {
  if (pimpl_)
    return pimpl_->panel.GetIsReposition();
  else
    return false;
}

void UI::Show() {
  if (pimpl_) {
    pimpl_->Show();
  }
}

void UI::Hide() {
  if (pimpl_) {
    pimpl_->Hide();
  }
}

void UI::ShowWithTimeout(size_t millisec) {
  if (pimpl_) {
    pimpl_->ShowWithTimeout(millisec);
  }
}

bool UI::IsCountingDown() const {
  return pimpl_ && pimpl_->timer != 0;
}

bool UI::IsShown() const {
  return pimpl_ && pimpl_->IsShown();
}

void UI::Refresh() {
  if (pimpl_) {
    pimpl_->Refresh();
  }
}

void UI::UpdateInputPosition(RECT const& rc) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    pimpl_->panel.MoveTo(rc);
  }
}

void UI::Update(const Context& ctx, const Status& status) {
  if (!pimpl_) {
    // 窗口尚未创建（TSF 首次组合前）：不存在并发读者，直接落状态
    ctx_ = ctx;
    status_ = status;
    return;
  }
  // 写 ctx_/status_ 与 dedup 判断都收敛到 UI 线程（panel 内部 marshal），
  // 调用线程（服务端为 IPC 工作线程）不再触碰这些共享状态
  pimpl_->Update(ctx, status);
}

void UI::SetStyle(const UIStyle& style) {
  if (!pimpl_) {
    style_ = style;
    return;
  }
  pimpl_->SetStyle(style);
}
