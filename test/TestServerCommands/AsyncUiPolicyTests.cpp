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
using weasel::tsf::ShouldShowCandidateUI;

static_assert(!ShouldBeginCandidateUI(false, true),
              "candidate UI must not begin before a composition exists");
static_assert(ShouldBeginCandidateUI(true, true),
              "a created composition may begin UI for its current generation");
static_assert(!ShouldBeginCandidateUI(true, false),
              "a stale composition start must not begin candidate UI");

static_assert(ShouldShowCandidateUI(true, true, true),
              "a focused composing target may show its native candidate UI");
static_assert(!ShouldShowCandidateUI(true, true, false),
              "an async response for an unfocused document must stay hidden");
static_assert(!ShouldShowCandidateUI(true, false, true),
              "UI-less hosts own candidate rendering when pbShow is false");
static_assert(!ShouldShowCandidateUI(false, true, true),
              "non-composing state must never keep the candidate window shown");

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

  if (!tsf._IsPendingUIGenerationCurrent(generation))
    return false;

  tsf._InvalidatePendingUI();
  if (tsf._IsPendingUIGenerationCurrent(generation))
    return false;

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
