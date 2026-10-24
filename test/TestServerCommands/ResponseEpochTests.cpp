#include "../../WeaselTSF/ResponseEpoch.h"

#include <cstdio>
#include <cstdlib>

namespace {

using weasel::tsf::ClassifyEpochDisposition;
using weasel::tsf::CommitDisposition;
using weasel::tsf::EpochDisposition;
using weasel::tsf::kInvalidResponseSerial;
using weasel::tsf::ResponseIdentity;
using weasel::tsf::ShouldEnqueueResponse;
using weasel::tsf::ShouldKeepCommitAcrossEpochReset;
using weasel::tsf::ShouldResetEngineAfterFailure;
using weasel::tsf::ShouldResetEngineAfterResponseFailure;

static_assert(ShouldEnqueueResponse(ResponseIdentity{7, 3}, 6, 3),
              "a fresh response in the current engine epoch must enqueue");
static_assert(!ShouldEnqueueResponse(ResponseIdentity{7, 3}, 7, 3),
              "the same response serial must enqueue at most once");
static_assert(
    !ShouldEnqueueResponse(ResponseIdentity{7, 2}, 6, 3),
    "a snapshot from an old engine epoch must never enqueue as fresh state");
static_assert(
    !ShouldEnqueueResponse(ResponseIdentity{kInvalidResponseSerial, 3}, 6, 3),
    "an invalid response serial must never enqueue");

static_assert(ShouldKeepCommitAcrossEpochReset(CommitDisposition::Pending),
              "a server-generated commit is durable across focus/epoch reset");
static_assert(!ShouldKeepCommitAcrossEpochReset(CommitDisposition::None),
              "state-only snapshots do not survive an engine reset");

static_assert(ClassifyEpochDisposition(3, 3, false) ==
                  EpochDisposition::ApplyState,
              "current-epoch state must apply normally");
static_assert(ClassifyEpochDisposition(2, 3, false) == EpochDisposition::Drop,
              "stale state without a commit must be discarded");
static_assert(ClassifyEpochDisposition(2, 3, true) ==
                  EpochDisposition::ApplyCommitOnly,
              "stale responses may preserve only their durable commit event");

static_assert(
    ShouldResetEngineAfterFailure(EpochDisposition::ApplyState),
    "a failed current-epoch state transition must resynchronize Rime");
static_assert(!ShouldResetEngineAfterFailure(EpochDisposition::ApplyCommitOnly),
              "an undeliverable stale commit must not clear the current epoch");
static_assert(!ShouldResetEngineAfterFailure(EpochDisposition::Drop),
              "discarded stale state must not clear the current epoch");
static_assert(
    ShouldResetEngineAfterResponseFailure(ResponseIdentity{9, 4}, 4, true),
    "a current-epoch failure still resets even when request storage moves");
static_assert(
    !ShouldResetEngineAfterResponseFailure(ResponseIdentity{9, 3}, 4, true),
    "a copied stale-commit identity must not reset the current epoch");
static_assert(ShouldResetEngineAfterResponseFailure(
                  ResponseIdentity{kInvalidResponseSerial, 0},
                  4,
                  false),
              "untracked compatibility work retains legacy reset behavior");

struct ResponseEpochRegressionGuard {
  ResponseEpochRegressionGuard() {
    if (ClassifyEpochDisposition(9, 9, false) != EpochDisposition::ApplyState) {
      std::fputs("response epoch regression\n", stderr);
      std::abort();
    }
  }
};

ResponseEpochRegressionGuard g_response_epoch_regression_guard;

}  // namespace
