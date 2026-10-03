#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // if server connection is Not OK, don't eat it.
  if (!_EnsureServerConnected()) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
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
    // 模拟期间的键不转发服务器，但 *pfEaten 必须有确定值
    // （下文无条件存入 _prevfEaten，且要上报 TSF）
    *pfEaten = FALSE;
    if (!_keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (_prevKeyEvent.keycode == ibus::Caps_Lock && _prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!_keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            _keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (_keyCountToSimulate)
        _keyCountToSimulate--;
    }

    _prevfEaten = *pfEaten;
    _prevKeyEvent = ke;
  }
}

STDMETHODIMP WeaselTSF::OnSetFocus(BOOL fForeground) {
  // 焦点切换后，任何等待配对回调的 pending 状态都已失效
  _testKeyDownPending.pending = FALSE;
  _testKeyUpPending.pending = FALSE;
  if (fForeground)
    m_client.FocusIn();
  else {
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
 * OnKeyDown() to check if the key has already been sent to the server. The
 * pending state is keyed by virtual-key code only: the paired Test/Key
 * callbacks are not guaranteed to carry identical lParam (for non-TSF apps
 * the caller is the CUAS IMM32-bridge, which may pass different lParam bits
 * between the two calls), while the vkey is the whole key identity this
 * component consumes (see KeyEvent.cpp). Any event for a different key
 * starts a new cycle, so a stale pending flag (its paired call was
 * swallowed) cannot eat the next key.
 */

STDMETHODIMP WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      BOOL* pfEaten) {
  _testKeyUpPending.pending = FALSE;
  if (_testKeyDownPending.pending && _testKeyDownPending.vkey == wParam) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  _testKeyDownPending.pending = *pfEaten;
  _testKeyDownPending.vkey = static_cast<UINT>(wParam);
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyDown(ITfContext* pContext,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL* pfEaten) {
  _testKeyUpPending.pending = FALSE;
  BOOL fDuplicate =
      _testKeyDownPending.pending && _testKeyDownPending.vkey == wParam;
  _testKeyDownPending.pending = FALSE;
  if (fDuplicate) {
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
  _testKeyDownPending.pending = FALSE;
  if (_testKeyUpPending.pending && _testKeyUpPending.vkey == wParam) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  _testKeyUpPending.pending = *pfEaten;
  _testKeyUpPending.vkey = static_cast<UINT>(wParam);
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyUp(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  _testKeyDownPending.pending = FALSE;
  BOOL fDuplicate =
      _testKeyUpPending.pending && _testKeyUpPending.vkey == wParam;
  _testKeyUpPending.pending = FALSE;
  if (fDuplicate) {
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

  if (!IsEqualGUID(rguid, GUID_WEASEL_QUICK_WORD))
    return S_OK;

  // Keep the shortcut local to an active Weasel input context.  If the
  // service is unavailable, let the application receive the key instead of
  // swallowing it.
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled())
    return S_OK;
  if (!_EnsureServerConnected())
    return S_OK;

  *pfEaten = m_client.OpenQuickWord() ? TRUE : FALSE;
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
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return TRUE;

  TF_PRESERVEDKEY quickWordKey = {};
  quickWordKey.uVKey = 'U';
  quickWordKey.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;

  static constexpr WCHAR kDescription[] = L"Weasel quick word";
  // A PreserveKey failure is deliberately non-fatal.  Hosts or another TIP
  // may reserve the same combination; Weasel must still activate normally.
  pKeystrokeMgr->PreserveKey(_tfClientId, GUID_WEASEL_QUICK_WORD,
                             &quickWordKey, kDescription,
                             _countof(kDescription) - 1);
  return TRUE;
}

void WeaselTSF::_UninitPreservedKey() {
  if (!_pThreadMgr)
    return;

  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  TF_PRESERVEDKEY quickWordKey = {};
  quickWordKey.uVKey = 'U';
  quickWordKey.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
  pKeystrokeMgr->UnpreserveKey(_tfClientId, GUID_WEASEL_QUICK_WORD,
                               &quickWordKey);
}
