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
         failure == ApplyFailureClass::Retryable && retries < kMaxApplyRetries;
}

// Destruction without DoEditSession means the host abandoned an asynchronous
// request. Only its current flight may retire the accepted candidate UI;
// RestoreFront retries and stale edit sessions must leave it untouched.
constexpr bool ShouldRetireCandidateOnApplyDispose(bool executed,
                                                  bool terminalFailure,
                                                  bool ownsFlight) {
  return ownsFlight && (terminalFailure || !executed);
}

// A host may release a queued asynchronous handoff without ever granting its
// edit lock. Re-requesting immediately can loop forever without consuming the
// retry budget. Only the still-current, unexecuted flight may give up its
// abandoned composition ownership and advance the pending response queue.
constexpr bool ShouldRetireAbandonedHandoff(bool executed,
                                           bool ownsFlight,
                                           bool sameComposition) {
  return !executed && ownsFlight && sameComposition;
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
