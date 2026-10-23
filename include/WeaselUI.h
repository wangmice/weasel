#pragma once
#include <WeaselIPCData.h>
#include <functional>
#include <memory>
#include <string>
#include <windows.h>
#include <wrl.h>
#include <WeaselUtility.h>

namespace weasel {
using namespace Microsoft::WRL;

template <typename T>
using an = std::shared_ptr<T>;
template <typename T>
using the = std::unique_ptr<T>;
typedef std::function<
    void(size_t* const, size_t* const, bool* const, bool* const)>
    UICallbackFunc;

enum ClientCapabilities {
  INLINE_PREEDIT_CAPABLE = 1,
};

// Preserve WNDPROC state if bounded owner-thread teardown cannot complete.
struct UISharedState {
  Context ctx;
  Status status;
  UIStyle style;
  UIStyle ostyle;
  bool in_server = false;
  UICallbackFunc callback;
};

class UIImpl;
class UI {
 public:
  UI();
  virtual ~UI();
  // 创建输入法界面
  bool Create(HWND parent, bool preview_mode = false);
  // 销毁界面
  void Destroy(bool full = false);
  // 界面显隐
  void Show();
  void Hide();
  void ShowWithTimeout(size_t millisec);
  BOOL IsCountingDown() const;
  BOOL IsShown() const;
  // 重绘界面
  void Refresh();
  // Reattach an undragged preview window to its parent dialog.
  void RepositionPreview();
  // 置输入焦点位置（光标跟随时移动候选窗）但不重绘
  void UpdateInputPosition(RECT const& rc);
  // Updates and style writes are marshalled to the window thread.
  void Update(Context const& ctx, Status const& status);
  void SetStyle(UIStyle const& style);
  Context& ctx() { return state_->ctx; }
  // Last context that completed a panel paint. Kept as an accessor rather
  // than a second UI-owned copy so fork paint-recovery tests can distinguish
  // state delivery from an actually rendered frame.
  Context& octx();
  Status& status() { return state_->status; }
  UIStyle& style() { return state_->style; }
  UIStyle& ostyle() { return state_->ostyle; }
  bool GetIsReposition();
  UICallbackFunc& uiCallback() { return state_->callback; }
  // compatibility shim for existing call sites
  void SetUICallBack(const UICallbackFunc& func) { state_->callback = func; }
  void SetCallback(const UICallbackFunc& func) { state_->callback = func; }
  bool& InServer() { return state_->in_server; }
  std::shared_ptr<UISharedState> SharedState() const { return state_; }
  HWND hwnd();

 private:
  std::shared_ptr<UISharedState> state_;
  the<UIImpl> pimpl_;
};
}  // namespace weasel
