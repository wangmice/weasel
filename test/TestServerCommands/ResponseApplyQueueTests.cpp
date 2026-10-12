#include "../../WeaselTSF/ResponseApplyQueue.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

bool ResponseApplyQueueIsSingleFlightAndFifo() {
  weasel::tsf::SerialApplyQueue<std::shared_ptr<int>> queue;
  queue.Push(std::make_shared<int>(1));
  queue.Push(std::make_shared<int>(2));

  // A context-handoff barrier occupies the one flight slot without consuming
  // the response at the queue head.
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

  // A RequestEditSession failure must put the exact request back at the head,
  // ahead of work that arrived later.
  if (!queue.RestoreFront(std::move(second), secondToken) ||
      !queue.CanSchedule())
    return false;
  auto restored = queue.TakeNext();
  const auto restoredToken = queue.FlightToken();
  if (!restored || *restored != 2)
    return false;

  // Deactivation/reset invalidates callbacks from an older flight.
  queue.Reset();
  return !queue.InFlight() && queue.PendingCount() == 0 &&
         !queue.Complete(restoredToken);
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
