#pragma once

#include <cstdint>

namespace weasel::tsf {

// Tracks an eaten OnTestKey* call until its paired OnKey* call arrives.
//
// Some hosts invoke OnTestKey* more than once for the same Windows input
// message, while others omit the paired OnKey* after an eaten test callback.
// A virtual key alone cannot distinguish those cases from a later physical
// repeat of the same key. GetMessageTime() gives the Test side a per-message
// identity without relying on lParam equality (which CUAS/IMM bridges do not
// guarantee between Test and Key callbacks).
struct PendingTestKeyState {
  bool IsDuplicateTest(std::uint32_t key, std::uint32_t message_time) const {
    return pending && vkey == key && test_message_time == message_time;
  }

  void Arm(std::uint32_t key, std::uint32_t message_time) {
    pending = true;
    vkey = key;
    test_message_time = message_time;
  }

  // Any real OnKey* callback ends the previous Test cycle. It is a duplicate
  // only when the virtual key matches; a different key starts a new cycle.
  bool ConsumePairedKey(std::uint32_t key) {
    const bool duplicate = pending && vkey == key;
    Clear();
    return duplicate;
  }

  void Clear() {
    pending = false;
    vkey = 0;
    test_message_time = 0;
  }

  bool pending = false;
  std::uint32_t vkey = 0;
  std::uint32_t test_message_time = 0;
};

}  // namespace weasel::tsf
