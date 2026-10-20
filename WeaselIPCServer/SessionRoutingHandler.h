#pragma once

#include <WeaselIPC.h>
#include <WeaselSessionLifecycle.h>

#include "SessionManager.h"

#include <cwchar>
#include <optional>
#include <string_view>

namespace weasel {

// RequestHandler decorator that owns logical START_SESSION identity policy.
// ServerImpl already serializes every RequestHandler call under g_api_mutex;
// this class deliberately has no mutex of its own. Named-pipe connections are
// transport only: current_owner_ identifies the client process generation, not
// a specific pipe/thread.
class SessionRoutingHandler : public RequestHandler {
 public:
  void SetBackend(RequestHandler* backend) noexcept { backend_ = backend; }
  RequestHandler* Backend() const noexcept { return backend_; }

  void SetCurrentOwner(SessionOwner owner) noexcept { current_owner_ = owner; }
  void ClearCurrentOwner() noexcept { current_owner_ = {}; }

  void RemoveClient(ClientProcessToken owner) {
    if (!backend_)
      return;
    manager_.RemoveClient(owner,
                          [this](DWORD id) { DestroyBackendSession(id); });
  }

  void Initialize() override {
    if (backend_)
      backend_->Initialize();
  }

  void Finalize() override {
    if (backend_)
      backend_->Finalize();
    // Backend Finalize is the authoritative global teardown; its session table
    // is already gone, so only clear lifecycle indexes here.
    manager_.Clear({});
    current_owner_ = {};
  }

  bool IsReadyForSessions() const override {
    return backend_ && backend_->IsReadyForSessions();
  }

  DWORD FindSession(DWORD session_id) override {
    return backend_ ? backend_->FindSession(session_id) : 0;
  }

  DWORD AddSession(LPWSTR buffer, EatLine eat = 0) override {
    if (!backend_ || !buffer)
      return 0;

    const std::size_t length = wcsnlen_s(buffer, WEASEL_IPC_BUFFER_LENGTH);
    if (length == WEASEL_IPC_BUFFER_LENGTH)
      return 0;
    const ParsedSessionKeyMetadata metadata =
        ParseSessionKeyMetadata(std::wstring_view(buffer, length));

    if (metadata.state == SessionKeyMetadataState::Invalid)
      return 0;

    if (metadata.state == SessionKeyMetadataState::Absent) {
      const DWORD id = backend_->AddSession(buffer, eat);
      if (!id)
        return 0;
      if (!manager_.BindLegacy(id, current_owner_)) {
        DestroyBackendSession(id);
        return 0;
      }
      return id;
    }

    const SessionManager::EnsureResult result = manager_.Ensure(
        *metadata.key, current_owner_,
        [this](DWORD id) { return backend_->FindSession(id) == id; },
        [this, buffer, eat]() -> std::optional<DWORD> {
          const DWORD id = backend_->AddSession(buffer, eat);
          return id ? std::optional<DWORD>(id) : std::nullopt;
        },
        [this](DWORD id) { DestroyBackendSession(id); });

    if (!result.ipc_id)
      return 0;

    // Newly-created AddSession already emitted the normal START response.
    // A retry reuses backend state but must stage a fresh response body for the
    // current transport transaction.
    if (!result.created && !backend_->RespondSession(result.ipc_id, eat)) {
      manager_.Remove(result.ipc_id,
                      [this](DWORD id) { DestroyBackendSession(id); });
      return 0;
    }
    return result.ipc_id;
  }

  bool RespondSession(DWORD session_id, EatLine eat) override {
    return backend_ && backend_->RespondSession(session_id, eat);
  }

  DWORD RemoveSession(DWORD session_id) override {
    if (!backend_)
      return 0;
    if (manager_.Remove(session_id,
                        [this](DWORD id) { DestroyBackendSession(id); }))
      return 0;
    return backend_->RemoveSession(session_id);
  }

  BOOL ProcessKeyEvent(KeyEvent keyEvent,
                       DWORD session_id,
                       EatLine eat) override {
    return backend_ ? backend_->ProcessKeyEvent(keyEvent, session_id, eat)
                    : FALSE;
  }

  void CommitComposition(DWORD session_id, EatLine eat) override {
    if (backend_)
      backend_->CommitComposition(session_id, eat);
  }

  void ClearComposition(DWORD session_id) override {
    if (backend_)
      backend_->ClearComposition(session_id);
  }

  void SelectCandidateOnCurrentPage(size_t index,
                                    DWORD session_id,
                                    EatLine eat) override {
    if (backend_)
      backend_->SelectCandidateOnCurrentPage(index, session_id, eat);
  }

  bool HighlightCandidateOnCurrentPage(size_t index,
                                       DWORD session_id,
                                       EatLine eat) override {
    return backend_ &&
           backend_->HighlightCandidateOnCurrentPage(index, session_id, eat);
  }

  bool ChangePage(bool backward, DWORD session_id, EatLine eat) override {
    return backend_ && backend_->ChangePage(backward, session_id, eat);
  }

  void FocusIn(DWORD param, DWORD session_id) override {
    if (backend_)
      backend_->FocusIn(param, session_id);
  }

  void FocusOut(DWORD param, DWORD session_id) override {
    if (backend_)
      backend_->FocusOut(param, session_id);
  }

  void UpdateInputPosition(RECT const& rc, DWORD session_id) override {
    if (backend_)
      backend_->UpdateInputPosition(rc, session_id);
  }

  void StartMaintenance() override {
    if (backend_)
      backend_->StartMaintenance();
    // Backend maintenance destroys/invalidates every live session itself.
    manager_.Clear({});
    current_owner_ = {};
  }

  void EndMaintenance() override {
    if (backend_)
      backend_->EndMaintenance();
  }

  void SetOption(DWORD session_id, const std::string& opt, bool val) override {
    if (backend_)
      backend_->SetOption(session_id, opt, val);
  }

  void UpdateColorTheme(BOOL dark_mode) override {
    if (backend_)
      backend_->UpdateColorTheme(dark_mode);
  }

 private:
  void DestroyBackendSession(DWORD id) {
    if (!backend_)
      return;
    // FindSession may itself purge an already-stale Rime mapping. Avoid a
    // second destroy in that case. Valid sessions use the lifecycle hook so a
    // background owner exit cannot hide another process's active UI.
    if (backend_->FindSession(id) == id)
      backend_->DestroySession(id);
  }

  RequestHandler* backend_ = nullptr;
  SessionOwner current_owner_;
  SessionManager manager_;
};

}  // namespace weasel
