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
// retry budget. Only the still-current, unexecuted flight may initiate
// bounded cleanup while the pending response FIFO remains paused.
constexpr bool ShouldRetireAbandonedHandoff(bool executed,
                                            bool ownsFlight,
                                            bool sameComposition) {
  return !executed && ownsFlight && sameComposition;
}

inline constexpr std::uint64_t kAbandonedHandoffRetryCooldownMs = 500;

constexpr bool ShouldScheduleAbandonedHandoffCleanup(bool outstanding,
                                                     unsigned attempts) {
  return !outstanding && attempts < kMaxApplyRetries;
}

// Budget exhaustion is deferred, not permanent. Only a later edit notification
// on the same old context can rearm a new bounded burst.
constexpr bool ShouldRearmAbandonedHandoffCleanup(bool deferred,
                                                  bool outstanding,
                                                  std::uint64_t elapsed_ms) {
  return deferred && !outstanding &&
         elapsed_ms >= kAbandonedHandoffRetryCooldownMs;
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
