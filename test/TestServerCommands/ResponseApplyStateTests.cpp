#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselTSF/ResponseApplyState.h"

#include <cstdio>
#include <cstdlib>

namespace {

using weasel::tsf::ApplyFailureClass;
using weasel::tsf::ApplyProgress;
using weasel::tsf::CanRetryApplyFailure;
using weasel::tsf::ClassifyScheduleFailure;
using weasel::tsf::kMaxApplyRetries;
using weasel::tsf::RetryBudget;
using weasel::tsf::ScheduleFailureAction;
using weasel::tsf::ShouldClearCompositionRange;
using weasel::tsf::ShouldPublishCandidateImmediately;
using weasel::tsf::ShouldRetireAbandonedHandoff;
using weasel::tsf::ShouldRetireCandidateOnApplyDispose;
using weasel::tsf::ShouldScheduleAbandonedHandoffCleanup;

static_assert(CanRetryApplyFailure(ApplyProgress::BeforeCommit,
                                   ApplyFailureClass::Retryable,
                                   0),
              "a transient pre-commit failure may retry safely");
static_assert(!CanRetryApplyFailure(ApplyProgress::CommitApplied,
                                    ApplyFailureClass::Retryable,
                                    0),
              "a committed response must never replay its commit text");
static_assert(!CanRetryApplyFailure(ApplyProgress::BeforeCommit,
                                    ApplyFailureClass::Terminal,
                                    0),
              "terminal document failures must not pin the FIFO");
static_assert(!CanRetryApplyFailure(ApplyProgress::BeforeCommit,
                                    ApplyFailureClass::Retryable,
                                    kMaxApplyRetries),
              "retries must be bounded so one document cannot pin the FIFO");

constexpr RetryBudget kIndependentBudgets{2, 0};
static_assert(!kIndependentBudgets.CanRetryHandoff(),
              "handoff retries must have their own bound");
static_assert(kIndependentBudgets.CanRetryApply(),
              "handoff failures must not consume document-apply retries");

static_assert(
    ClassifyScheduleFailure(false, false, true) ==
        ScheduleFailureAction::RetryLater,
    "TF_E_LOCKED must preserve the request for a later edit-end retry");
static_assert(ClassifyScheduleFailure(true, false, false) ==
                  ScheduleFailureAction::Drop,
              "a disconnected context cannot ever consume its queued response");
static_assert(ClassifyScheduleFailure(false, true, false) ==
                  ScheduleFailureAction::Drop,
              "a read-only context must not be restored forever at FIFO head");
static_assert(
    ClassifyScheduleFailure(false, false, false) == ScheduleFailureAction::Drop,
    "non-lock scheduling failures must not create an unbounded retry");

static_assert(ShouldClearCompositionRange(true, true),
              "the current composition may be cleared during an abort");
static_assert(!ShouldClearCompositionRange(true, false),
              "a stale async end session must never clear committed text");
static_assert(!ShouldClearCompositionRange(false, true),
              "normal commit teardown must preserve its composition text");

static_assert(ShouldRetireAbandonedHandoff(false, true, true),
              "abandoned current async handoff must start cleanup");
static_assert(ShouldScheduleAbandonedHandoffCleanup(false, 0),
              "initial abandoned handoff cleanup can be requested");
static_assert(ShouldScheduleAbandonedHandoffCleanup(false, 1),
              "a later edit-end can retry after an abandoned cleanup");
static_assert(!ShouldScheduleAbandonedHandoffCleanup(true, 1),
              "an outstanding cleanup session must not be duplicated");
static_assert(!ShouldScheduleAbandonedHandoffCleanup(false, kMaxApplyRetries),
              "repeatedly discarded cleanup sessions must be bounded");
static_assert(!ShouldRetireAbandonedHandoff(true, true, true),
              "completed handoffs must not retire a new composition");
static_assert(!ShouldRetireAbandonedHandoff(false, false, true),
              "old flight destructors cannot retire a newer handoff");
static_assert(!ShouldRetireAbandonedHandoff(false, true, false),
              "a no-longer-current composition must not be retired");

static_assert(ShouldRetireCandidateOnApplyDispose(false, false, true),
              "abandoned asynchronous edits must release their candidate UI");
static_assert(ShouldRetireCandidateOnApplyDispose(true, true, true),
              "terminal apply failures must release their candidate UI");
static_assert(!ShouldRetireCandidateOnApplyDispose(true, false, true),
              "successfully executed edits must not retire a new UIElement");
static_assert(!ShouldRetireCandidateOnApplyDispose(false, false, false),
              "a stale edit session must not retire another flight's UI");
static_assert(!ShouldRetireCandidateOnApplyDispose(false, true, false),
              "restored retries must keep their candidate UI until handled");

static_assert(
    ShouldPublishCandidateImmediately(true),
    "UI-less hosts need candidate data before caret extent completes");
static_assert(!ShouldPublishCandidateImmediately(false),
              "owned candidate windows may still position before first show");

struct ResponseApplyStateRegressionGuard {
  ResponseApplyStateRegressionGuard() {
    if (!CanRetryApplyFailure(ApplyProgress::BeforeCommit,
                              ApplyFailureClass::Retryable, 0)) {
      std::fputs("response apply state regression\n", stderr);
      std::abort();
    }
  }
};

ResponseApplyStateRegressionGuard g_response_apply_state_regression_guard;

}  // namespace
