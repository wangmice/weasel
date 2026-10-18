#include "WeaselPanel.h"
#include <WeaselUI.h>

namespace weasel {

// ----------------------------------------------------------------------------
class UIImpl {
 public:
  WeaselPanel panel;
  UIImpl(UI& ui) : panel(ui) {}
  ~UIImpl() {}
  bool IsShown() const { return panel.IsShown(); }
  void Refresh() {
    if (!panel.IsWindow())
      return;
    panel.Refresh();
  }
  void RepositionPreview() {
    if (panel.IsWindow())
      panel.RepositionPreview();
  }
  void Update(const Context& ctx, const Status& status) {
    panel.ApplyUpdate(ctx, status);
  }
  void SetStyle(const UIStyle& style) { panel.ApplyStyle(style); }
  void Show() { panel.ShowWindow(SW_SHOWNA); }
  void Hide() { panel.ShowWindow(SW_HIDE); }
  void ShowWithTimeout(size_t millisec) { panel.ShowWithTimeout(millisec); }
  bool IsCountingDown() const { return panel.IsCountingDown(); }
};
// ----------------------------------------------------------------------------
BOOL UI::IsCountingDown() const {
  return pimpl_ && pimpl_->panel.IsCountingDown();
};

UI::UI() : pimpl_(nullptr), in_server_(false) {}

UI::~UI() {
  if (pimpl_)
    Destroy(true);
}
BOOL UI::IsShown() const {
  return pimpl_ && pimpl_->IsShown();
}
void UI::UpdateInputPosition(RECT const& rc) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    pimpl_->panel.MoveTo(rc);
  }
}
void UI::Update(const Context& ctx, const Status& status) {
  if (!pimpl_) {
    ctx_ = ctx;
    status_ = status;
    return;
  }
  pimpl_->Update(ctx, status);
}

Context& UI::octx() {
  return pimpl_ ? pimpl_->panel.PaintedContext() : ctx_;
}

void UI::SetStyle(const UIStyle& style) {
  if (!pimpl_) {
    style_ = style;
    return;
  }
  pimpl_->SetStyle(style);
}
void UI::Refresh() {
  if (pimpl_) {
    pimpl_->Refresh();
  }
}
void UI::RepositionPreview() {
  if (pimpl_)
    pimpl_->RepositionPreview();
}
void UI::ShowWithTimeout(size_t millisec) {
  if (pimpl_) {
    pimpl_->ShowWithTimeout(millisec);
  }
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
void UI::Destroy(bool full) {
  if (pimpl_) {
    if (pimpl_->panel.IsWindow())
      pimpl_->panel.DestroyWindow();
    if (full) {
      // Release the panel; shared devices remain available to other UI objects.
      pimpl_->panel.ReleaseAllResources();
      pimpl_.reset();
    }
  }
}
bool UI::Create(HWND parent, bool preview_mode) {
  if (pimpl_) {
    pimpl_->panel.SetPreviewMode(preview_mode);
    if (!pimpl_->panel.IsWindow())
      pimpl_->panel.InvalidateWindowSizeCache();
    return pimpl_->panel.Create(parent, preview_mode);
  }
  pimpl_ = std::make_unique<UIImpl>(*this);
  if (!pimpl_)
    return false;
  return pimpl_->panel.Create(parent, preview_mode);
}
bool UI::GetIsReposition() {
  return pimpl_ && pimpl_->panel.GetIsReposition();
}

HWND UI::hwnd() {
  if (pimpl_ && pimpl_->panel.IsWindow())
    return pimpl_->panel.hwnd();
  return nullptr;
}
}  // namespace weasel
