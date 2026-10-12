#include "../../WeaselTSF/ResponseEpoch.h"

#include <cstdio>
#include <cstdlib>

namespace {

using weasel::tsf::CommitDisposition;
using weasel::tsf::ResponseIdentity;
using weasel::tsf::ShouldEnqueueResponse;
using weasel::tsf::ShouldKeepCommitAcrossEpochReset;
using weasel::tsf::kInvalidResponseSerial;

static_assert(ShouldEnqueueResponse(ResponseIdentity{7, 3}, 6, 3),
              "a fresh response in the current engine epoch must enqueue");
static_assert(!ShouldEnqueueResponse(ResponseIdentity{7, 3}, 7, 3),
              "the same response serial must enqueue at most once");
static_assert(!ShouldEnqueueResponse(ResponseIdentity{7, 2}, 6, 3),
              "a snapshot from an old engine epoch must never apply");
static_assert(!ShouldEnqueueResponse(ResponseIdentity{kInvalidResponseSerial, 3},
                                     6, 3),
              "an invalid response serial must never enqueue");

static_assert(ShouldKeepCommitAcrossEpochReset(CommitDisposition::Pending),
              "a server-generated commit is durable across focus/epoch reset");
static_assert(!ShouldKeepCommitAcrossEpochReset(CommitDisposition::None),
              "state-only snapshots do not survive an engine reset");

struct ResponseEpochRegressionGuard {
  ResponseEpochRegressionGuard() {
    if (!ShouldEnqueueResponse(ResponseIdentity{9, 4}, 8, 4)) {
      std::fputs("response epoch regression\n", stderr);
      std::abort();
    }
  }
};

ResponseEpochRegressionGuard g_response_epoch_regression_guard;

}  // namespace
