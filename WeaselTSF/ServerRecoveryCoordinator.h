#pragma once
#include <Windows.h>

namespace weasel::tsf {

// Process-local failure-path throttle. A small SRW lock is deliberately used
// here instead of another atomic state machine: this path is never touched by
// healthy typing, and admission/backoff updates become one linearizable
// critical section.
class ServerRecoveryCoordinator {
 public:
  bool WorkerInflight() const noexcept {
    SharedGuard guard(gate_);
    return worker_inflight_;
  }

  ULONGLONG RetryAfter() const noexcept {
    SharedGuard guard(gate_);
    return retry_after_;
  }

  ULONGLONG UnavailableSince() const noexcept {
    SharedGuard guard(gate_);
    return unavailable_since_;
  }

  LONG FailureStreak() const noexcept {
    SharedGuard guard(gate_);
    return failure_streak_;
  }

  ULONGLONG HealthRevision() const noexcept {
    SharedGuard guard(gate_);
    return health_revision_;
  }

  template <typename Action>
  bool RunIfHealthUnchanged(ULONGLONG expected_revision,
                            Action&& action) noexcept {
    ExclusiveGuard guard(gate_);
    if (health_revision_ != expected_revision)
      return false;
    return action();
  }

  bool TryBegin(ULONGLONG now) noexcept {
    ExclusiveGuard guard(gate_);
    if (worker_inflight_ || now < retry_after_)
      return false;
    worker_inflight_ = true;
    return true;
  }

  void ResetUnavailableObservation() noexcept {
    ExclusiveGuard guard(gate_);
    unavailable_since_ = 0;
  }

  ULONGLONG UnavailableDuration(ULONGLONG now) const noexcept {
    SharedGuard guard(gate_);
    if (!unavailable_since_)
      return 0;
    return now >= unavailable_since_ ? now - unavailable_since_ : 0;
  }

  bool ObserveUnavailableLongEnough(ULONGLONG now,
                                    DWORD threshold_ms) noexcept {
    ExclusiveGuard guard(gate_);
    if (!unavailable_since_)
      unavailable_since_ = now;
    return now - unavailable_since_ >= threshold_ms;
  }

  void ResetFailureStreak() noexcept {
    ExclusiveGuard guard(gate_);
    failure_streak_ = 0;
  }

  void NoteHealthy() noexcept {
    ExclusiveGuard guard(gate_);
    ++health_revision_;
    // A successful foreground reconnect breaks the continuous-unavailability
    // interval even when the existing recovery worker keeps admission
    // ownership. The worker is not cancelled here; its revision check/fresh
    // probe decides whether it may still perform destructive recovery.
    failure_streak_ = 0;
    unavailable_since_ = 0;
    retry_after_ = 0;
  }

  void FinishHealthy() noexcept {
    ExclusiveGuard guard(gate_);
    failure_streak_ = 0;
    unavailable_since_ = 0;
    retry_after_ = 0;
    worker_inflight_ = false;
  }

  void FinishBackoff(DWORD delay_ms, ULONGLONG now) noexcept {
    ExclusiveGuard guard(gate_);
    retry_after_ = now + delay_ms;
    worker_inflight_ = false;
  }

  void FinishFailure(ULONGLONG now,
                     DWORD base_delay_ms,
                     DWORD max_delay_ms) noexcept {
    ExclusiveGuard guard(gate_);
    ++failure_streak_;
    const unsigned int shift =
        failure_streak_ <= 1
            ? 0U
            : (failure_streak_ >= 4
                   ? 3U
                   : static_cast<unsigned int>(failure_streak_ - 1));
    DWORD delay_ms = base_delay_ms << shift;
    if (delay_ms > max_delay_ms)
      delay_ms = max_delay_ms;
    retry_after_ = now + delay_ms;
    worker_inflight_ = false;
  }

 private:
  class SharedGuard {
   public:
    explicit SharedGuard(SRWLOCK& lock) : lock_(lock) {
      AcquireSRWLockShared(&lock_);
    }
    ~SharedGuard() { ReleaseSRWLockShared(&lock_); }

   private:
    SRWLOCK& lock_;
  };

  class ExclusiveGuard {
   public:
    explicit ExclusiveGuard(SRWLOCK& lock) : lock_(lock) {
      AcquireSRWLockExclusive(&lock_);
    }
    ~ExclusiveGuard() { ReleaseSRWLockExclusive(&lock_); }

   private:
    SRWLOCK& lock_;
  };

  mutable SRWLOCK gate_ = SRWLOCK_INIT;
  bool worker_inflight_ = false;
  ULONGLONG retry_after_ = 0;
  ULONGLONG unavailable_since_ = 0;
  LONG failure_streak_ = 0;
  ULONGLONG health_revision_ = 0;
};

}  // namespace weasel::tsf
