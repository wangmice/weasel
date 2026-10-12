#include "../../WeaselTSF/ResponseApplyQueue.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

bool ResponseApplyQueueIsSingleFlightAndFifo() {
  weasel::tsf::SerialApplyQueue<std::shared_ptr<int>> queue;
  const auto initialEpoch = queue.CurrentEpoch();
  if (initialEpoch == 0 || queue.LastEnqueuedSerial() != 0)
    return false;
  if (!queue.MarkSerialEnqueued(7) || queue.MarkSerialEnqueued(7) ||
      queue.LastEnqueuedSerial() != 7)
    return false;

  queue.Push(std::make_shared<int>(1));
  queue.Push(std::make_shared<int>(2));

  queue.AdvanceEpoch();
  if (queue.CurrentEpoch() == initialEpoch || queue.PendingCount() != 2 ||
      queue.LastEnqueuedSerial() != 0)
    return false;

  const auto pauseToken = queue.Pause();
  if (pauseToken == 0 || queue.CanSchedule() || !queue.IsPaused() ||
      queue.PendingCount() != 2)
    return false;
  if (queue.Resume(pauseToken + 1) || !queue.IsPaused())
    return false;
  if (!queue.Resume(pauseToken) || !queue.CanSchedule() || queue.IsPaused())
    return false;

  if (!queue.BeginBarrier())
    return false;
  const auto barrierToken = queue.FlightToken();
  if (barrierToken == 0 || !queue.InFlight() || queue.PendingCount() != 2)
    return false;
  if (!queue.Complete(barrierToken))
    return false;

  if (!queue.CanSchedule())
    return false;
  auto peek = queue.PeekFront();
  if (!peek || *peek != 1)
    return false;
  auto first = queue.TakeNext();
  const auto firstToken = queue.FlightToken();
  if (!first || *first != 1 || firstToken == 0 || firstToken == barrierToken ||
      queue.CanSchedule())
    return false;
  if (!queue.Complete(firstToken))
    return false;

  if (!queue.CanSchedule())
    return false;
  auto second = queue.TakeNext();
  const auto secondToken = queue.FlightToken();
  if (!second || *second != 2)
    return false;

  // A lock-conflict retry is not run recursively inside the same lock stack.
  // It is explicitly requested and consumed once by OnEndEdit after the
  // competing write session releases the context.
  if (!queue.RestoreFront(std::move(second), secondToken) ||
      !queue.CanSchedule())
    return false;
  queue.RequestRetry();
  if (!queue.ConsumeRetryRequest() || queue.ConsumeRetryRequest())
    return false;

  auto restored = queue.TakeNext();
  const auto restoredToken = queue.FlightToken();
  if (!restored || *restored != 2)
    return false;

  const auto epochBeforeReset = queue.CurrentEpoch();
  const auto stalePauseToken = queue.Pause();
  queue.RequestRetry();
  queue.Reset();
  if (queue.CurrentEpoch() == epochBeforeReset || queue.PendingCount() != 0 ||
      queue.LastEnqueuedSerial() != 0 || queue.IsPaused() ||
      queue.ConsumeRetryRequest())
    return false;
  if (queue.Resume(stalePauseToken))
    return false;

  queue.Push(std::make_shared<int>(3));
  auto current = queue.TakeNext();
  const auto currentToken = queue.FlightToken();
  if (!current || *current != 3 || currentToken == restoredToken)
    return false;
  if (queue.Complete(restoredToken))
    return false;
  if (!queue.InFlight() || queue.FlightToken() != currentToken)
    return false;
  return queue.Complete(currentToken) && !queue.InFlight() &&
         queue.PendingCount() == 0;
}

static_assert(
    weasel::tsf::ShouldEndCandidateUIForContextHandoff(true, false),
    "cross-context composition handoff must end the old candidate UI");
static_assert(
    !weasel::tsf::ShouldEndCandidateUIForContextHandoff(true, true),
    "same-context replacement may retain the candidate UI lifetime");
static_assert(weasel::tsf::ShouldHandleCompositionContextEvent(true, true),
              "the owning context may inspect its live composition");
static_assert(!weasel::tsf::ShouldHandleCompositionContextEvent(true, false),
              "a foreign context callback must not inspect the composition");

struct ResponseApplyQueueRegressionGuard {
  ResponseApplyQueueRegressionGuard() {
    if (!ResponseApplyQueueIsSingleFlightAndFifo()) {
      std::fputs("ordered response apply queue regression\n", stderr);
      std::abort();
    }
  }
};

ResponseApplyQueueRegressionGuard g_response_apply_queue_regression_guard;

}  // namespace
