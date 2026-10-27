#include "../../WeaselTSF/ResponseApplyQueue.h"
#include "../../WeaselTSF/ResponseApplyState.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
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

// Model TSF's COM behavior: RequestEditSession accepts a session, then the
// host drops its last reference without ever calling DoEditSession. The TIP
// must park the owning flight and never request again in the same Release
// stack. An old-context notification or a later input response can release it.
struct FakeComHandoffHost {
  weasel::tsf::SerialApplyQueue<std::shared_ptr<int>> fifo;
  std::uint64_t waiting_token = 0;
  unsigned edit_requests = 0;
  weasel::tsf::RetryBudget retries;
  bool old_composition = true;
  unsigned retired_compositions = 0;

  bool DropAcceptedAsyncSession() {
    if (!old_composition || !fifo.BeginBarrier())
      return false;
    const auto flight = fifo.FlightToken();
    ++edit_requests;
    // CContextHandoffEditSession::~CContextHandoffEditSession
    // parks the queue before completing its owned flight.
    waiting_token = fifo.Pause();
    return fifo.Complete(flight);
  }

  void OnInputResponse(int value) {
    fifo.Push(std::make_shared<int>(value));
    const auto action = weasel::tsf::ClassifyHandoffInput(
        waiting_token != 0 && fifo.IsPaused() &&
            fifo.PauseToken() == waiting_token,
        fifo.InFlight(), retries.handoff_retries);
    if (action == weasel::tsf::HandoffInputAction::KeepWaiting)
      return;
    if (action == weasel::tsf::HandoffInputAction::Retry) {
      ++retries.handoff_retries;
    } else {
      old_composition = false;
      ++retired_compositions;
    }
    const auto token = waiting_token;
    waiting_token = 0;
    fifo.Resume(token);
  }

  void OnEndEdit(bool from_original_context) {
    if (!from_original_context || waiting_token == 0)
      return;
    const auto token = waiting_token;
    waiting_token = 0;
    fifo.Resume(token);
  }

  bool OnOldCompositionTerminated() {
    if (waiting_token == 0)
      return false;
    OnEndEdit(true);
    return fifo.CanSchedule();
  }
};

bool AbandonedHandoffFaultInjection() {
  FakeComHandoffHost host;
  host.fifo.Push(std::make_shared<int>(7));
  host.fifo.Push(std::make_shared<int>(8));

  if (!host.DropAcceptedAsyncSession() || host.edit_requests != 1 ||
      !host.fifo.IsPaused() || host.fifo.CanSchedule())
    return false;
  const auto stale_pause_token = host.waiting_token;

  // Additional responses and edits in other documents cannot recursively
  // request the same abandoned handoff or lose pending durable text.
  host.fifo.Push(std::make_shared<int>(9));
  host.OnEndEdit(false);
  if (host.edit_requests != 1 || !host.fifo.IsPaused() ||
      host.fifo.PendingCount() != 3)
    return false;

  // The original document's next edit allows one new attempt; if that is
  // also abandoned, the host must wait for another real edit notification.
  host.OnEndEdit(true);
  if (!host.fifo.CanSchedule() || !host.DropAcceptedAsyncSession() ||
      host.edit_requests != 2 || !host.fifo.IsPaused() ||
      host.fifo.Resume(stale_pause_token))
    return false;
  if (!host.OnOldCompositionTerminated() || host.edit_requests != 2)
    return false;

  for (int expected : {7, 8, 9}) {
    auto next = host.fifo.TakeNext();
    const auto token = host.fifo.FlightToken();
    if (!next || *next != expected || !host.fifo.Complete(token))
      return false;
  }
  if (host.fifo.PendingCount() != 0)
    return false;

  // Cancellation invalidates any host-held pause/flight token.
  host.fifo.Push(std::make_shared<int>(10));
  if (!host.DropAcceptedAsyncSession())
    return false;
  const auto canceled = host.waiting_token;
  host.fifo.Reset();
  return !host.fifo.Resume(canceled) && !host.fifo.InFlight() &&
         !host.fifo.IsPaused() && host.fifo.PendingCount() == 0;
}

bool NewInputRecoversWithoutOldContextNotifications() {
  FakeComHandoffHost host;
  host.fifo.Push(std::make_shared<int>(7));
  if (!host.DropAcceptedAsyncSession())
    return false;

  // The old context never sends an edit or composition-termination callback.
  // New input must wake the queue without requesting inside COM destruction.
  for (int value : {8, 9}) {
    const auto requests = host.edit_requests;
    host.OnInputResponse(value);
    if (!host.fifo.CanSchedule() || host.edit_requests != requests ||
        !host.old_composition || !host.DropAcceptedAsyncSession())
      return false;
  }
  if (host.retries.handoff_retries != weasel::tsf::kMaxApplyRetries ||
      host.edit_requests != 3)
    return false;

  // Repeated abandonment cannot keep consuming keys indefinitely. Retire the
  // old composition at the next input boundary and preserve queued commits.
  host.OnInputResponse(10);
  if (!host.fifo.CanSchedule() || host.old_composition ||
      host.retired_compositions != 1 || host.DropAcceptedAsyncSession())
    return false;
  for (int expected : {7, 8, 9, 10}) {
    auto next = host.fifo.TakeNext();
    const auto token = host.fifo.FlightToken();
    if (!next || *next != expected || !host.fifo.Complete(token))
      return false;
  }
  return host.fifo.PendingCount() == 0;
}

bool InputRecoveryPreservesNewerPauseAndActiveFlight() {
  FakeComHandoffHost host;
  host.fifo.Push(std::make_shared<int>(1));
  if (!host.DropAcceptedAsyncSession())
    return false;
  const auto newerPause = host.fifo.Pause();
  host.OnInputResponse(2);
  if (!host.fifo.IsPaused() || host.fifo.PauseToken() != newerPause ||
      host.retries.handoff_retries != 0 || host.retired_compositions != 0)
    return false;

  host.fifo.Reset();
  host.fifo.Push(std::make_shared<int>(3));
  if (!host.fifo.BeginBarrier())
    return false;
  const auto flight = host.fifo.FlightToken();
  host.waiting_token = host.fifo.Pause();
  host.OnInputResponse(4);
  return host.fifo.InFlight() && host.fifo.FlightToken() == flight &&
         host.fifo.IsPaused() && host.retries.handoff_retries == 0 &&
         host.retired_compositions == 0;
}

static_assert(
    weasel::tsf::ShouldEndCandidateUIForContextHandoff(true, false),
    "cross-context composition handoff must end the old candidate UI");
static_assert(!weasel::tsf::ShouldEndCandidateUIForContextHandoff(true, true),
              "same-context replacement may retain the candidate UI lifetime");
static_assert(weasel::tsf::ShouldHandleCompositionContextEvent(true, true),
              "the owning context may inspect its live composition");
static_assert(!weasel::tsf::ShouldHandleCompositionContextEvent(true, false),
              "a foreign context callback must not inspect the composition");

struct ResponseApplyQueueRegressionGuard {
  ResponseApplyQueueRegressionGuard() {
    if (!ResponseApplyQueueIsSingleFlightAndFifo() ||
        !AbandonedHandoffFaultInjection() ||
        !NewInputRecoversWithoutOldContextNotifications() ||
        !InputRecoveryPreservesNewerPauseAndActiveFlight()) {
      std::fputs("ordered response apply queue regression\n", stderr);
      std::abort();
    }
  }
};

ResponseApplyQueueRegressionGuard g_response_apply_queue_regression_guard;

}  // namespace

