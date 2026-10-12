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

}  // namespace weasel::tsf
