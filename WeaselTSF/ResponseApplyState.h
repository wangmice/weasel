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

constexpr bool ShouldPublishCandidateImmediately(bool uiLess) {
  return uiLess;
}

}  // namespace weasel::tsf
