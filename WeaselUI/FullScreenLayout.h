#pragma once

#include "StandardLayout.h"

namespace weasel {
class FullScreenLayout : public StandardLayout {
 public:
  FullScreenLayout(const UIStyle& style,
                   const Context& context,
                   const Status& status,
                   const RECT& inputPos,
                   the<Layout> layout,
                   an<D2D>& pD2D)
      : StandardLayout(style, context, status, pD2D),
        mr_inputPos(inputPos),
        m_layout(std::move(layout)) {}

  virtual void DoLayout();

 private:
  bool AdjustFontPoint(const CRect& workArea, int& step);

  // Reference the panel's persistent RECT directly; converting to CRect
  // would bind this member to a temporary destroyed after construction.
  const RECT& mr_inputPos;
  the<Layout> m_layout;
};
};  // namespace weasel
