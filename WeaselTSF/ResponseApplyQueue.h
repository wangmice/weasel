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

  bool CanSchedule() const { return !in_flight_ && !pending_.empty(); }
  bool InFlight() const { return in_flight_; }
  std::uint64_t FlightToken() const { return flight_token_; }
  std::size_t PendingCount() const { return pending_.size(); }
  T PeekFront() const { return pending_.empty() ? T{} : pending_.front(); }

  bool BeginBarrier() {
    if (in_flight_)
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

  void RequestRetry() { retry_pending_ = true; }

  bool ConsumeRetryRequest() {
    const bool requested = retry_pending_;
    retry_pending_ = false;
    return requested;
  }

  void Reset() {
    pending_.clear();
    in_flight_ = false;
    retry_pending_ = false;
    AdvanceFlightToken();
  }

 private:
  void AdvanceFlightToken() {
    ++flight_token_;
    if (flight_token_ == 0)
      ++flight_token_;
  }

  std::deque<T> pending_;
  bool in_flight_ = false;
  bool retry_pending_ = false;
  std::uint64_t flight_token_ = 0;
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
