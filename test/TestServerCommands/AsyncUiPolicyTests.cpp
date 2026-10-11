#include "../../WeaselTSF/stdafx.h"

#define private public
#include "../../WeaselTSF/WeaselTSF.h"
#undef private

#include "../../WeaselTSF/AsyncUiPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

using weasel::tsf::ShouldBeginCandidateUI;

static_assert(!ShouldBeginCandidateUI(false, true),
              "candidate UI must not begin before a composition exists");
static_assert(ShouldBeginCandidateUI(true, true),
              "a created composition may begin UI for its current generation");
static_assert(!ShouldBeginCandidateUI(true, false),
              "a stale composition start must not begin candidate UI");

bool CurrentGenerationSurvivesPendingSnapshotConsumption() {
  WeaselTSF tsf;
  auto context = std::make_shared<weasel::Context>();
  context->preedit.str = L"current";
  weasel::Status status;
  status.composing = true;

  const UINT64 generation = tsf._QueuePendingUI(context, status);
  std::shared_ptr<weasel::Context> taken;
  weasel::Status taken_status;
  if (!tsf._TakePendingUI(generation, taken, taken_status))
    return false;

  // Layout/inline-preedit callbacks can run after the candidate snapshot was
  // already painted. They still belong to this response generation until a
  // newer response or teardown invalidates it.
  if (!tsf._IsPendingUIGenerationCurrent(generation))
    return false;

  tsf._InvalidatePendingUI();
  if (tsf._IsPendingUIGenerationCurrent(generation))
    return false;

  // Invalidation advances the serial, but the new numeric value is not itself
  // an active response generation until _QueuePendingUI installs a context.
  return !tsf._IsPendingUIGenerationCurrent(tsf._pendingUIGeneration);
}

struct AsyncUiPolicyRegressionGuard {
  AsyncUiPolicyRegressionGuard() {
    if (!CurrentGenerationSurvivesPendingSnapshotConsumption()) {
      std::fputs("async UI generation lifetime regression\n", stderr);
      std::abort();
    }
  }
};

AsyncUiPolicyRegressionGuard g_async_ui_policy_regression_guard;

}  // namespace
