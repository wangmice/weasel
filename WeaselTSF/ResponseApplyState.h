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

// A host may abandon a queued asynchronous handoff without granting an
// edit lock. Its active flight can arm one old-context edit barrier; no
// immediate same-stack request should be scheduled on COM destruction.
constexpr bool ShouldWaitForAbandonedHandoff(bool executed,
                                             bool ownsFlight,
                                             bool sameComposition) {
  return !executed && ownsFlight && sameComposition;
}

enum class HandoffInputAction : std::uint8_t {
  KeepWaiting,
  Retry,
  RetireComposition,
};

// A later input response is another safe retry boundary when the old context
// never sends OnEndEdit. A stale pause owner or an executing flight must not
// be disturbed. Repeated abandonment shares the normal handoff retry budget.
constexpr HandoffInputAction ClassifyHandoffInput(bool ownsPause,
                                                 bool inFlight,
                                                 unsigned retries) {
  if (!ownsPause || inFlight)
    return HandoffInputAction::KeepWaiting;
  return retries < kMaxApplyRetries
             ? HandoffInputAction::Retry
             : HandoffInputAction::RetireComposition;
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

