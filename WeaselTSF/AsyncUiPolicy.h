#pragma once

namespace weasel::tsf {

constexpr bool ShouldBeginCandidateUI(bool compositionCreated,
                                      bool generationCurrent) {
  return compositionCreated && generationCurrent;
}

constexpr bool ShouldShowCandidateUI(bool composing,
                                     bool hostShow,
                                     bool targetFocused) {
  return composing && hostShow && targetFocused;
}

// A confirmed host selection finishes one UIElement generation, but Rime may
// immediately offer another group of candidates in the same composition.
constexpr bool ShouldRenewCandidateUIAfterFinalization(bool accepted,
                                                       bool sameContext,
                                                       bool composing) {
  return accepted && sameContext && composing;
}

}  // namespace weasel::tsf
