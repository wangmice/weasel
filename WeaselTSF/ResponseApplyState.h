#pragma once

#include <cstdint>

namespace weasel::tsf {

enum class ApplyProgress : std::uint8_t {
  BeforeCommit,
  CommitApplied,
};

enum class ApplyFailureClass : std::uint8_t {
  Retryable,
  Terminal,
};

inline constexpr unsigned kMaxApplyRetries = 2;

struct RetryBudget {
  unsigned handoff_retries = 0;
  unsigned apply_retries = 0;

  constexpr bool CanRetryHandoff() const {
    return handoff_retries < kMaxApplyRetries;
  }

  constexpr bool CanRetryApply() const {
    return apply_retries < kMaxApplyRetries;
  }
};

constexpr bool CanRetryApplyFailure(ApplyProgress progress,
                                    ApplyFailureClass failure,
                                    unsigned retries) {
  return progress == ApplyProgress::BeforeCommit &&
         failure == ApplyFailureClass::Retryable &&
         retries < kMaxApplyRetries;
}

enum class ScheduleFailureAction : std::uint8_t {
  RetryLater,
  Drop,
};

constexpr ScheduleFailureAction ClassifyScheduleFailure(bool disconnected,
                                                         bool readOnly,
                                                         bool locked) {
  if (disconnected || readOnly)
    return ScheduleFailureAction::Drop;
  if (locked)
    return ScheduleFailureAction::RetryLater;
  return ScheduleFailureAction::Drop;
}

constexpr bool ShouldClearCompositionRange(bool clearRequested,
                                           bool compositionStillCurrent) {
  return clearRequested && compositionStillCurrent;
}

constexpr bool ShouldPublishCandidateImmediately(bool uiLess) {
  return uiLess;
}

}  // namespace weasel::tsf
