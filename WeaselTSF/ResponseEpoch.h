#pragma once

#include <cstdint>

namespace weasel::tsf {

inline constexpr std::uint64_t kInvalidResponseSerial = 0;

struct ResponseIdentity {
  std::uint64_t serial = kInvalidResponseSerial;
  std::uint64_t engine_epoch = 0;
};

enum class CommitDisposition : std::uint8_t {
  None,
  Pending,
};

constexpr bool ShouldEnqueueResponse(ResponseIdentity response,
                                     std::uint64_t last_enqueued_serial,
                                     std::uint64_t current_engine_epoch) {
  return response.serial != kInvalidResponseSerial &&
         response.serial != last_enqueued_serial &&
         response.engine_epoch == current_engine_epoch;
}

constexpr bool ShouldKeepCommitAcrossEpochReset(CommitDisposition disposition) {
  return disposition == CommitDisposition::Pending;
}

}  // namespace weasel::tsf
