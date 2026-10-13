#pragma once

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

constexpr bool KeepNotificationVisible(bool notification_scheduled,
                                       bool counting_down) {
  return notification_scheduled || counting_down;
}

}  // namespace weasel::interaction_policy
