#pragma once

#include <atomic>
#include <cstdint>

namespace weasel::interaction_policy {

constexpr bool EchoConfirmsSession(std::uint32_t expected,
                                   std::uint32_t current,
                                   std::uint32_t echoed) {
  return expected != 0 && current == expected && echoed == expected;
}

constexpr bool NeedsRimeSessionValidation(std::uint64_t validated_epoch,
                                          std::uint64_t cleanup_epoch) {
  return validated_epoch != cleanup_epoch;
}

constexpr bool CanUseEnhancedForegroundBounds(
    bool enhanced_position,
    bool has_foreground_window,
    bool has_foreground_rect) noexcept {
  return enhanced_position && has_foreground_window && has_foreground_rect;
}

constexpr bool ShouldFinalizeEndSession(bool session_ending) noexcept {
  return session_ending;
}

class PendingAutoHideState {
 public:
  void OnPost() noexcept {
    pending_.fetch_add(1, std::memory_order_relaxed);
  }

  void OnPostFailed() noexcept { ConsumeOne(); }
  void OnApplied() noexcept { ConsumeOne(); }

  bool Pending() const noexcept {
    return pending_.load(std::memory_order_relaxed) != 0;
  }

  void Reset() noexcept { pending_.store(0, std::memory_order_relaxed); }

 private:
  void ConsumeOne() noexcept {
    auto current = pending_.load(std::memory_order_relaxed);
    while (current != 0 &&
           !pending_.compare_exchange_weak(current, current - 1,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) {
    }
  }

  std::atomic<std::uint32_t> pending_{0};
};

}  // namespace weasel::interaction_policy
