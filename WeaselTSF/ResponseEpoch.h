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

enum class EpochDisposition : std::uint8_t {
  ApplyState,
  ApplyCommitOnly,
  Drop,
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

constexpr EpochDisposition ClassifyEpochDisposition(
    std::uint64_t response_epoch,
    std::uint64_t current_engine_epoch,
    bool has_commit) {
  if (response_epoch == current_engine_epoch)
    return EpochDisposition::ApplyState;
  return has_commit ? EpochDisposition::ApplyCommitOnly
                    : EpochDisposition::Drop;
}

constexpr bool ShouldResetEngineAfterFailure(EpochDisposition disposition) {
  return disposition == EpochDisposition::ApplyState;
}

constexpr bool ShouldResetEngineAfterResponseFailure(
    ResponseIdentity response,
    std::uint64_t current_engine_epoch,
    bool has_commit) {
  if (response.serial == kInvalidResponseSerial)
    return true;
  return ShouldResetEngineAfterFailure(ClassifyEpochDisposition(
      response.engine_epoch, current_engine_epoch, has_commit));
}

}  // namespace weasel::tsf
