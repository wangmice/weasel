#pragma once

namespace weasel::tsf {

constexpr bool ShouldBeginCandidateUI(bool compositionCreated,
                                      bool generationCurrent) {
  return compositionCreated && generationCurrent;
}

}  // namespace weasel::tsf
