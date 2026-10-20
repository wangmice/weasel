#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"

WeaselTSF::KeyDispatchResult WeaselTSF::_ProcessKeyEventWithRecovery(
    const weasel::KeyEvent& keyEvent,
    ULONGLONG recovery_deadline) {
  const auto ensure_recovery_deadline = [&]() {
    if (!recovery_deadline) {
      recovery_deadline = GetTickCount64() + kFastSessionRecoveryBudgetMs;
    }
    return recovery_deadline;
  };
  const auto remaining_recovery_time = [&](ULONGLONG deadline) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline)
      return static_cast<DWORD>(0);
    return static_cast<DWORD>(deadline - now);
  };
  const auto process_key_bounded = [&]() {
    return m_client.ProcessKeyEventEx(keyEvent, kKeyEventTransactionTimeoutMs);
  };

  // The successful hot path remains one IPC transaction, but a wedged server
  // can hold the TSF callback for at most 100 ms instead of the pipe default
  // of one second.
  auto result = process_key_bounded();

  if (result == weasel::KeyEventResult::TransportNotDelivered) {
    // The request definitely did not reach the server. Reconnect and session
    // establishment share the 20 ms recovery budget. Once recovered, the
    // actual key transaction gets its own bounded 100 ms response window;
    // squeezing it into the leftover recovery milliseconds would turn a fast
    // 15-20 ms recovery into a spurious ambiguous key timeout.
    const ULONGLONG deadline = ensure_recovery_deadline();
    if (!_EnsureServerConnectedUntil(deadline))
      return KeyDispatchResult::Unhandled;
    result = process_key_bounded();
  }

  if (result == weasel::KeyEventResult::InvalidSession) {
    // InvalidSession is emitted before librime receives the key, so one retry
    // is safe. Session rebuild is limited to the 20 ms recovery budget; the
    // replay itself still uses the bounded 100 ms physical-key IPC timeout.
    const ULONGLONG deadline = ensure_recovery_deadline();
    const DWORD remaining = remaining_recovery_time(deadline);
    if (!remaining) {
      _ScheduleServerRecovery();
      return KeyDispatchResult::Unhandled;
    }

    m_client.StartSession(remaining);
    _ConsumeResponseIfFresh();
    if (!m_client.IsSessionActive()) {
      _ScheduleServerRecovery();
      return KeyDispatchResult::Unhandled;
    }

    result = process_key_bounded();
  }

  // The request may have reached the server, so never replay an ambiguous
  // transport failure. Since the same physical key must not then be processed
  // by the host as well, report it as locally consumed while background
  // recovery repairs the transport/session for the next key.
  if (result == weasel::KeyEventResult::TransportError) {
    _ScheduleServerRecovery();
    return KeyDispatchResult::AmbiguousConsumed;
  }

  // A replay that is still definitely undelivered is safe to release to the
  // host; no server-side input engine has observed it.
  if (result == weasel::KeyEventResult::TransportNotDelivered) {
    _ScheduleServerRecovery();
    return KeyDispatchResult::Unhandled;
  }

  return result == weasel::KeyEventResult::Handled
             ? KeyDispatchResult::Handled
             : KeyDispatchResult::Unhandled;
}

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // Allocate the recovery deadline only on the inactive-session path. Healthy
  // typing keeps the same local IsSessionActive() check and never reads a
  // clock.
  ULONGLONG recovery_deadline = 0;
  if (!m_client.IsSessionActive()) {
    recovery_deadline = GetTickCount64() + kFastSessionRecoveryBudgetMs;
    if (!_EnsureServerConnectedUntil(recovery_deadline)) {
      *pfEaten = FALSE;
      return;
    }
  }
  weasel::KeyEvent ke;
  if (!GetKeyboardState(_lpbKeyState)) {
    // Do not interpret a key using stale modifier state from the previous
    // event. When Windows cannot provide a current keyboard snapshot, release
    // the key to the host and try again on the next event.
    *pfEaten = FALSE;
    return;
  }
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    // Only callbacks that were already expected on entry may consume the
    // CapsLock simulation budget. The real CapsLock release that schedules
    // SendInput must not immediately consume one of the callbacks it just
    // queued.
    const bool suppress_simulated_caps =
        _keyCountToSimulate > 0 && ke.keycode == ibus::Caps_Lock;

    // Chrome and other hosts can drop injected callbacks. If a real
    // non-CapsLock key arrives first, discard the stale simulation budget and
    // process that same key normally instead of bypassing Rime indefinitely.
    if (_keyCountToSimulate && !suppress_simulated_caps)
      _keyCountToSimulate = 0;

    // Simulated CapsLock callbacks are local bookkeeping only; every other
    // key follows the normal server path.
    *pfEaten = FALSE;
    if (!suppress_simulated_caps) {
      const KeyDispatchResult dispatch =
          _ProcessKeyEventWithRecovery(ke, recovery_deadline);
      *pfEaten = dispatch == KeyDispatchResult::Unhandled ? FALSE : TRUE;
    }

    if (ke.keycode == ibus::Caps_Lock) {
      if (_prevKeyEvent.keycode == ibus::Caps_Lock && _prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && !suppress_simulated_caps) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            // Track exactly the callbacks Windows accepted. This assignment
            // happens after suppress_simulated_caps was captured, so the
            // current real CapsLock release does not consume the new budget.
            _keyCountToSimulate = static_cast<int>(::SendInput(
                sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT)));
          }
        }
        *pfEaten = TRUE;
      }
      if (suppress_simulated_caps && _keyCountToSimulate)
        _keyCountToSimulate--;
    }

    _prevfEaten = *pfEaten;
    _prevKeyEvent = ke;
  }
}

STDMETHODIMP WeaselTSF::OnSetFocus(BOOL fForeground) {
  // 焦点切换后，任何等待配对回调和 CapsLock 模拟的瞬态状态都已失效。
  // Chrome 可在 DocumentMgr/renderer 之间切换焦点而不回送注入事件。
  _testKeyDownPending.Clear();
  _testKeyUpPending.Clear();
  _keyCountToSimulate = 0;
  _prevKeyEvent = weasel::KeyEvent();
  _prevfEaten = FALSE;
  if (fForeground)
    m_client.FocusIn();
  else {
    // A response already emitted by Rime may still be waiting for its WRITE
    // edit session. Keep that FIFO intact: _AbortComposition advances the
    // engine epoch so stale state becomes inert while durable commits survive.
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}

/* Some apps sends strange OnTestKeyDown/OnKeyDown combinations:
 *  Some sends OnKeyDown() only. (QQ2012)
 *  Some sends multiple OnTestKeyDown() for a single key event. (MS WORD 2010
 * x64)
 *  Some swallows the key after an eaten OnTestKeyDown(), so the paired
 * OnKeyDown() never comes.
 *
 * We use the pending state to omit multiple OnTestKeyDown() calls, and for
 * OnKeyDown() to check if the key has already been sent to the server. Test
 * and Key callbacks are paired by virtual-key plus GetMessageTime(): CUAS/IMM
 * bridges do not guarantee identical lParam, while message time distinguishes
 * a swallowed old Test callback from a later physical repeat of the same key.
 */

STDMETHODIMP WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      BOOL* pfEaten) {
  _testKeyUpPending.Clear();
  const auto vkey = static_cast<std::uint32_t>(wParam);
  const auto messageTime = static_cast<std::uint32_t>(::GetMessageTime());
  if (_testKeyDownPending.IsDuplicateTest(vkey, messageTime)) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _testKeyDownPending.Arm(vkey, messageTime);
  else
    _testKeyDownPending.Clear();
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyDown(ITfContext* pContext,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL* pfEaten) {
  _testKeyUpPending.Clear();
  const auto messageTime = static_cast<std::uint32_t>(::GetMessageTime());
  if (_testKeyDownPending.ConsumePairedKey(
          static_cast<std::uint32_t>(wParam), messageTime)) {
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    _UpdateComposition(pContext);
  }
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnTestKeyUp(ITfContext* pContext,
                                    WPARAM wParam,
                                    LPARAM lParam,
                                    BOOL* pfEaten) {
  _testKeyDownPending.Clear();
  const auto vkey = static_cast<std::uint32_t>(wParam);
  const auto messageTime = static_cast<std::uint32_t>(::GetMessageTime());
  if (_testKeyUpPending.IsDuplicateTest(vkey, messageTime)) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _testKeyUpPending.Arm(vkey, messageTime);
  else
    _testKeyUpPending.Clear();
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyUp(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  _testKeyDownPending.Clear();
  const auto messageTime = static_cast<std::uint32_t>(::GetMessageTime());
  if (_testKeyUpPending.ConsumePairedKey(static_cast<std::uint32_t>(wParam),
                                         messageTime)) {
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    _UpdateComposition(pContext);
  }
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                       REFGUID rguid,
                                       BOOL* pfEaten) {
  *pfEaten = FALSE;
  return S_OK;
}

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);

  return (hr == S_OK);
}

void WeaselTSF::_UninitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {}
