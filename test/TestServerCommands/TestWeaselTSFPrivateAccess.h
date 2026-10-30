#pragma once

#include "../../WeaselTSF/WeaselTSF.h"
#include "../../WeaselTSF/LanguageBar.h"
#include "../../WeaselTSF/CandidateList.h"

class WeaselTSFTestAccess {
 public:
  static weasel::Client& Client(WeaselTSF& tsf) { return tsf.m_client; }

  static void BindLanguageBar(WeaselTSF& tsf,
                              ITfThreadMgr* manager,
                              CLangBarItemButton* button) {
    tsf._pThreadMgr = manager;
    tsf._tfClientId = 1;
    tsf._pLangBarButton = button;
  }

  static void UpdateLanguageBar(WeaselTSF& tsf, weasel::Status status) {
    tsf._UpdateLanguageBar(status);
  }

  static com_ptr<CCandidateList> Candidate(WeaselTSF& tsf) { return tsf._cand; }

  static void StartCandidateUI(WeaselTSF& tsf,
                               ITfThreadMgr* manager,
                               ITfContext* context) {
    tsf._pThreadMgr = manager;
    tsf._tfClientId = 1;
    tsf._StartUI(context);
  }

  static void EndCandidateUI(WeaselTSF& tsf) { tsf._EndUI(); }

  static ITfContext* CandidateUIContext(const WeaselTSF& tsf) {
    return tsf._pCandidateUIContext;
  }

  static void RenewCandidateUI(WeaselTSF& tsf, weasel::Status status) {
    // Exercise the production renewal without registering a host UIElement.
    // StartUI(nullptr) leaves the private candidate's panel uncreated.
    tsf._RenewCandidateUI(nullptr, 0, weasel::Context{}, status);
  }

  static bool HasPendingUI(const WeaselTSF& tsf) { return tsf._hasPendingUI; }

  static auto& ResponseQueue(WeaselTSF& tsf) { return tsf._responseApplyQueue; }

  static void PauseAbandonedHandoff(WeaselTSF& tsf) {
    tsf._handoffWaitPauseToken = tsf._responseApplyQueue.Pause();
  }

  static void EnqueueInputResponse(WeaselTSF& tsf, const std::wstring& commit) {
    // Exercise the real enqueue/recovery path without requesting a host edit
    // session, registering a TIP, or contacting the live algorithm server.
    tsf._tfClientId = TF_CLIENTID_NULL;
    weasel::tsf::ResponseApplySnapshot snapshot;
    snapshot.commit = commit;
    tsf._EnqueueResponseApply(nullptr, std::move(snapshot), 0);
  }

  static void SetComposition(WeaselTSF& tsf,
                             ITfContext* context,
                             ITfComposition* composition) {
    tsf._tfClientId = 1;
    tsf._SetComposition(composition, context);
  }

  static void EnqueueContextResponse(WeaselTSF& tsf,
                                     ITfContext* context,
                                     const std::wstring& commit) {
    weasel::tsf::ResponseApplySnapshot snapshot;
    snapshot.context = std::make_shared<weasel::Context>();
    snapshot.commit = commit;
    snapshot.status.composing = false;
    tsf._EnqueueResponseApply(context, std::move(snapshot), 0);
  }

  static size_t RetainedCompositionCount(const WeaselTSF& tsf) {
    return tsf._retainedCompositions.size();
  }

  static void ResetCompositionState(WeaselTSF& tsf) {
    tsf._CancelHandoffWait();
    tsf._responseApplyQueue.Reset();
    tsf._retainedCompositions.clear();
    tsf._FinalizeComposition();
    tsf._tfClientId = TF_CLIENTID_NULL;
  }

  static bool ProcessKeyEventWithRecovery(WeaselTSF& tsf,
                                          const weasel::KeyEvent& key_event) {
    return tsf._ProcessKeyEventWithRecovery(key_event) !=
           WeaselTSF::KeyDispatchResult::Unhandled;
  }
};
