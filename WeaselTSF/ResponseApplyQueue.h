#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace weasel::tsf {

template <typename T>
class SerialApplyQueue {
 public:
  void Push(T item) { pending_.push_back(std::move(item)); }

  bool CanSchedule() const {
    return !paused_ && !in_flight_ && !pending_.empty();
  }
  bool InFlight() const { return in_flight_; }
  bool IsPaused() const { return paused_; }
  std::uint64_t FlightToken() const { return flight_token_; }
  std::uint64_t PauseToken() const { return pause_token_; }
  std::uint64_t CurrentEpoch() const { return engine_epoch_; }
  std::uint64_t LastEnqueuedSerial() const { return last_enqueued_serial_; }
  std::size_t PendingCount() const { return pending_.size(); }
  T PeekFront() const { return pending_.empty() ? T{} : pending_.front(); }

  template <typename Predicate>
  bool AnyPending(Predicate predicate) const {
    for (const auto& item : pending_) {
      if (predicate(item))
        return true;
    }
    return false;
  }

  bool MarkSerialEnqueued(std::uint64_t serial) {
    if (serial == 0 || serial == last_enqueued_serial_)
      return false;
    last_enqueued_serial_ = serial;
    return true;
  }

  void AdvanceEpoch() {
    ++engine_epoch_;
    if (engine_epoch_ == 0)
      ++engine_epoch_;
    last_enqueued_serial_ = 0;
  }

  std::uint64_t Pause() {
    paused_ = true;
    AdvancePauseToken();
    return pause_token_;
  }

  bool Resume(std::uint64_t token) {
    if (!paused_ || token == 0 || token != pause_token_)
      return false;
    paused_ = false;
    return true;
  }

  bool Resume() { return Resume(pause_token_); }

  void RequestRetry() { retry_pending_ = true; }

  bool ConsumeRetryRequest() {
    const bool requested = retry_pending_;
    retry_pending_ = false;
    return requested;
  }

  bool BeginBarrier() {
    if (paused_ || in_flight_)
      return false;
    in_flight_ = true;
    AdvanceFlightToken();
    return true;
  }

  T TakeNext() {
    if (!CanSchedule())
      return T{};
    in_flight_ = true;
    AdvanceFlightToken();
    T item = std::move(pending_.front());
    pending_.pop_front();
    return item;
  }

  bool RestoreFront(T item, std::uint64_t token) {
    if (!in_flight_ || token != flight_token_)
      return false;
    pending_.push_front(std::move(item));
    in_flight_ = false;
    return true;
  }

  bool Complete(std::uint64_t token) {
    if (!in_flight_ || token != flight_token_)
      return false;
    in_flight_ = false;
    return true;
  }

  void Reset() {
    pending_.clear();
    in_flight_ = false;
    paused_ = false;
    retry_pending_ = false;
    AdvanceFlightToken();
    AdvancePauseToken();
    AdvanceEpoch();
  }

 private:
  void AdvanceFlightToken() {
    ++flight_token_;
    if (flight_token_ == 0)
      ++flight_token_;
  }

  void AdvancePauseToken() {
    ++pause_token_;
    if (pause_token_ == 0)
      ++pause_token_;
  }

  std::deque<T> pending_;
  bool in_flight_ = false;
  bool paused_ = false;
  bool retry_pending_ = false;
  std::uint64_t flight_token_ = 0;
  std::uint64_t pause_token_ = 0;
  std::uint64_t engine_epoch_ = 1;
  std::uint64_t last_enqueued_serial_ = 0;
};

constexpr bool ShouldEndCandidateUIForContextHandoff(bool hasComposition,
                                                     bool sameContext) {
  return hasComposition && !sameContext;
}

constexpr bool ShouldHandleCompositionContextEvent(bool hasComposition,
                                                   bool sameContext) {
  return hasComposition && sameContext;
}

}  // namespace weasel::tsf
