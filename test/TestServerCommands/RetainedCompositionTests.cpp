// Exercise production handoff recovery with valid COM contexts, composition
// ranges and host-controlled asynchronous edit-session delivery. No live TIP,
// registration, algorithm server or user Rime data is touched.
#include "../../WeaselTSF/stdafx.h"
#include "TestWeaselTSFPrivateAccess.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace {

void Require(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "retained composition regression: %s\n", message);
    std::abort();
  }
}

template <typename Interface>
class TestComObject : public Interface {
 public:
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != __uuidof(Interface))
      return E_NOINTERFACE;
    *out = static_cast<Interface*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = --refs_;
    if (!remaining)
      delete this;
    return remaining;
  }

 protected:
  virtual ~TestComObject() = default;

 private:
  ULONG refs_ = 1;
};
class StubTfContext : public TestComObject<ITfContext> {
 public:
  STDMETHODIMP RequestEditSession(TfClientId tid,
                                  ITfEditSession* pes,
                                  DWORD dwFlags,
                                  HRESULT* phrSession) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP InWriteSession(TfClientId tid, BOOL* pfWriteSession) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetSelection(TfEditCookie ec,
                            ULONG ulIndex,
                            ULONG ulCount,
                            TF_SELECTION* pSelection,
                            ULONG* pcFetched) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP SetSelection(TfEditCookie ec,
                            ULONG ulCount,
                            const TF_SELECTION* pSelection) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetStart(TfEditCookie ec, ITfRange** ppStart) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetEnd(TfEditCookie ec, ITfRange** ppEnd) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetActiveView(ITfContextView** ppView) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumViews(IEnumTfContextViews** ppEnum) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetStatus(TF_STATUS* pdcs) override { return E_NOTIMPL; }
  STDMETHODIMP GetProperty(REFGUID guidProp, ITfProperty** ppProp) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetAppProperty(REFGUID guidProp,
                              ITfReadOnlyProperty** ppProp) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP TrackProperties(const GUID** prgProp,
                               ULONG cProp,
                               const GUID** prgAppProp,
                               ULONG cAppProp,
                               ITfReadOnlyProperty** ppProperty) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumProperties(IEnumTfProperties** ppEnum) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppDm) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP CreateRangeBackup(TfEditCookie ec,
                                 ITfRange* pRange,
                                 ITfRangeBackup** ppBackup) override {
    return E_NOTIMPL;
  }
};

class StubTfRange : public TestComObject<ITfRange> {
 public:
  STDMETHODIMP GetText(TfEditCookie ec,
                       DWORD dwFlags,
                       WCHAR* pchText,
                       ULONG cchMax,
                       ULONG* pcch) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP SetText(TfEditCookie ec,
                       DWORD dwFlags,
                       const WCHAR* pchText,
                       LONG cch) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetFormattedText(TfEditCookie ec,
                                IDataObject** ppDataObject) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetEmbedded(TfEditCookie ec,
                           REFGUID rguidService,
                           REFIID riid,
                           IUnknown** ppunk) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP InsertEmbedded(TfEditCookie ec,
                              DWORD dwFlags,
                              IDataObject* pDataObject) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftStart(TfEditCookie ec,
                          LONG cchReq,
                          LONG* pcch,
                          const TF_HALTCOND* pHalt) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftEnd(TfEditCookie ec,
                        LONG cchReq,
                        LONG* pcch,
                        const TF_HALTCOND* pHalt) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftStartToRange(TfEditCookie ec,
                                 ITfRange* pRange,
                                 TfAnchor aPos) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftEndToRange(TfEditCookie ec,
                               ITfRange* pRange,
                               TfAnchor aPos) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftStartRegion(TfEditCookie ec,
                                TfShiftDir dir,
                                BOOL* pfNoRegion) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftEndRegion(TfEditCookie ec,
                              TfShiftDir dir,
                              BOOL* pfNoRegion) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsEmpty(TfEditCookie ec, BOOL* pfEmpty) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP Collapse(TfEditCookie ec, TfAnchor aPos) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsEqualStart(TfEditCookie ec,
                            ITfRange* pWith,
                            TfAnchor aPos,
                            BOOL* pfEqual) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsEqualEnd(TfEditCookie ec,
                          ITfRange* pWith,
                          TfAnchor aPos,
                          BOOL* pfEqual) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP CompareStart(TfEditCookie ec,
                            ITfRange* pWith,
                            TfAnchor aPos,
                            LONG* plResult) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP CompareEnd(TfEditCookie ec,
                          ITfRange* pWith,
                          TfAnchor aPos,
                          LONG* plResult) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP AdjustForInsert(TfEditCookie ec,
                               ULONG cchInsert,
                               BOOL* pfInsertOk) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetGravity(TfGravity* pgStart, TfGravity* pgEnd) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP SetGravity(TfEditCookie ec,
                          TfGravity gStart,
                          TfGravity gEnd) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP Clone(ITfRange** ppClone) override { return E_NOTIMPL; }
  STDMETHODIMP GetContext(ITfContext** ppContext) override { return E_NOTIMPL; }
};

class StubTfComposition : public TestComObject<ITfComposition> {
 public:
  STDMETHODIMP GetRange(ITfRange** ppRange) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStart(TfEditCookie ecWrite, ITfRange* pNewStart) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ShiftEnd(TfEditCookie ecWrite, ITfRange* pNewEnd) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EndComposition(TfEditCookie ecWrite) override {
    return E_NOTIMPL;
  }
};

class StubTfInsertAtSelection : public TestComObject<ITfInsertAtSelection> {
 public:
  STDMETHODIMP InsertTextAtSelection(TfEditCookie ec,
                                     DWORD dwFlags,
                                     const WCHAR* pchText,
                                     LONG cch,
                                     ITfRange** ppRange) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie ec,
                                         DWORD dwFlags,
                                         IDataObject* pDataObject,
                                         ITfRange** ppRange) override {
    return E_NOTIMPL;
  }
};

class HostContext;
struct RangeState {
  size_t start = 0;
  size_t length = 0;
};

class HostRange : public StubTfRange {
 public:
  HostRange(HostContext* context, std::shared_ptr<RangeState> range);
  STDMETHODIMP SetText(TfEditCookie ec,
                       DWORD,
                       const WCHAR* text,
                       LONG count) override;
  STDMETHODIMP Collapse(TfEditCookie ec, TfAnchor anchor) override;
  STDMETHODIMP GetContext(ITfContext** context) override;
  size_t Start() const { return range_->start; }

 private:
  com_ptr<ITfContext> context_;
  std::shared_ptr<RangeState> range_;
};

class HostInsert : public StubTfInsertAtSelection {
 public:
  explicit HostInsert(HostContext* context) : context_(context) {}
  STDMETHODIMP InsertTextAtSelection(TfEditCookie ec,
                                     DWORD,
                                     const WCHAR* text,
                                     LONG count,
                                     ITfRange** range) override;

 private:
  HostContext* context_;
};

class HostContext : public StubTfContext {
 public:
  enum class Delivery { Drop, Hold, Grant, Disconnected };
  explicit HostContext(std::wstring value)
      : text(std::move(value)), caret(text.size()) {}
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    if (!out)
      return E_POINTER;
    if (iid == IID_ITfInsertAtSelection) {
      *out = static_cast<ITfInsertAtSelection*>(new HostInsert(this));
      return S_OK;
    }
    return StubTfContext::QueryInterface(iid, out);
  }
  STDMETHODIMP RequestEditSession(TfClientId,
                                  ITfEditSession* session,
                                  DWORD flags,
                                  HRESULT* result) override {
    if (delivery == Delivery::Disconnected)
      return TF_E_DISCONNECTED;
    if (delivery == Delivery::Drop) {
      *result = TF_S_ASYNC;
      return S_OK;
    }
    if (delivery == Delivery::Hold || (flags & TF_ES_ASYNC)) {
      pending.push_back(session);
      *result = TF_S_ASYNC;
      return S_OK;
    }
    *result = Grant(session);
    return S_OK;
  }
  STDMETHODIMP InWriteSession(TfClientId, BOOL* writing) override {
    *writing = write_depth != 0;
    return S_OK;
  }
  STDMETHODIMP SetSelection(TfEditCookie ec,
                            ULONG count,
                            const TF_SELECTION* selection) override {
    if (!Writable(ec))
      return TF_E_NOLOCK;
    if (count != 1)
      return E_INVALIDARG;
    caret = static_cast<HostRange*>(selection->range)->Start();
    return S_OK;
  }
  HRESULT Grant(ITfEditSession* session) {
    ++write_depth;
    const HRESULT hr = session->DoEditSession(kCookie);
    --write_depth;
    return hr;
  }
  void GrantOne() {
    Require(!pending.empty(), "expected a queued host edit session");
    const auto session = pending.front();
    pending.pop_front();
    Grant(session);
  }
  void GrantAll() {
    unsigned attempts = 0;
    while (!pending.empty()) {
      Require(++attempts < 32, "host edit delivery must remain bounded");
      GrantOne();
    }
  }
  bool Writable(TfEditCookie ec) {
    const bool allowed = ec == kCookie && write_depth != 0;
    if (!allowed)
      ++invalid_writes;
    return allowed;
  }
  static constexpr TfEditCookie kCookie = 42;
  Delivery delivery = Delivery::Grant;
  std::wstring text;
  size_t caret;
  unsigned write_depth = 0;
  unsigned invalid_writes = 0;
  std::deque<com_ptr<ITfEditSession>> pending;
};

HostRange::HostRange(HostContext* context, std::shared_ptr<RangeState> range)
    : context_(context), range_(std::move(range)) {}

HRESULT HostRange::SetText(TfEditCookie ec,
                           DWORD,
                           const WCHAR* text,
                           LONG count) {
  auto* context = static_cast<HostContext*>(context_.p);
  if (!context->Writable(ec))
    return TF_E_NOLOCK;
  if (count < 0 || range_->start + range_->length > context->text.size())
    return E_INVALIDARG;
  const auto old_end = range_->start + range_->length;
  context->text.replace(range_->start, range_->length, text, count);
  if (context->caret >= old_end)
    context->caret = context->caret - range_->length + count;
  range_->length = count;
  return S_OK;
}

HRESULT HostRange::Collapse(TfEditCookie ec, TfAnchor anchor) {
  auto* context = static_cast<HostContext*>(context_.p);
  if (!context->Writable(ec))
    return TF_E_NOLOCK;
  range_ = std::make_shared<RangeState>(*range_);
  if (anchor == TF_ANCHOR_END)
    range_->start += range_->length;
  range_->length = 0;
  return S_OK;
}

HRESULT HostRange::GetContext(ITfContext** context) {
  *context = context_;
  (*context)->AddRef();
  return S_OK;
}

HRESULT HostInsert::InsertTextAtSelection(TfEditCookie ec,
                                          DWORD,
                                          const WCHAR* text,
                                          LONG count,
                                          ITfRange** range) {
  if (!context_->Writable(ec))
    return TF_E_NOLOCK;
  auto state = std::make_shared<RangeState>();
  state->start = context_->caret;
  state->length = count;
  context_->text.insert(context_->caret, text, count);
  context_->caret += count;
  *range = new HostRange(context_, state);
  return S_OK;
}

class HostComposition : public StubTfComposition {
 public:
  HostComposition(HostContext* context, WeaselTSF* service)
      : context_(context),
        service_(service),
        range_(std::make_shared<RangeState>()) {
    range_->length = context->text.size();
  }
  STDMETHODIMP GetRange(ITfRange** range) override {
    if (!active)
      return E_UNEXPECTED;
    if (on_get_range)
      on_get_range();
    *range = new HostRange(static_cast<HostContext*>(context_.p), range_);
    return S_OK;
  }
  STDMETHODIMP EndComposition(TfEditCookie ec) override {
    auto* context = static_cast<HostContext*>(context_.p);
    if (!context->Writable(ec))
      return TF_E_NOLOCK;
    if (!active)
      return E_UNEXPECTED;
    active = false;
    ++end_calls;
    return service_->OnCompositionTerminated(ec, this);
  }
  bool active = true;
  unsigned end_calls = 0;
  std::function<void()> on_get_range;

 private:
  com_ptr<ITfContext> context_;
  WeaselTSF* service_;
  std::shared_ptr<RangeState> range_;
};

struct Fixture {
  WeaselTSF service;
  com_ptr<HostContext> old_context;
  com_ptr<HostContext> next_context;
  com_ptr<HostComposition> composition;
  Fixture() {
    old_context.Attach(new HostContext(L"ni"));
    next_context.Attach(new HostContext(L""));
    composition.Attach(new HostComposition(old_context, &service));
    WeaselTSFTestAccess::SetComposition(service, old_context, composition);
  }
  ~Fixture() {
    old_context->pending.clear();
    next_context->pending.clear();
    WeaselTSFTestAccess::ResetCompositionState(service);
  }
  void AbandonHandoff(HostContext::Delivery cleanup_delivery) {
    old_context->delivery = HostContext::Delivery::Drop;
    for (const wchar_t* commit : {L"B", L"C", L"D"})
      WeaselTSFTestAccess::EnqueueContextResponse(service, next_context,
                                                  commit);
    old_context->delivery = cleanup_delivery;
    WeaselTSFTestAccess::EnqueueContextResponse(service, next_context, L"E");
    next_context->GrantAll();
    Require(next_context->text == L"BCDE",
            "new document must keep every queued commit");
    Require(WeaselTSFTestAccess::ResponseQueue(service).PendingCount() == 0,
            "unresponsive old context must not pin the response FIFO");
  }
};

void CleanupRequiresTheOriginalWriteLock() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Hold);
  Require(
      fixture.composition->active && fixture.old_context->text == L"ni" &&
          WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 1,
      "exhaustion must retain the still-live old composition");
  fixture.old_context->GrantOne();
  Require(
      !fixture.composition->active && fixture.composition->end_calls == 1 &&
          fixture.old_context->text.empty() &&
          WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 0,
      "granted cleanup must clear old preedit and end its host composition");
  Require(fixture.old_context->invalid_writes == 0,
          "cleanup must never write without the old context's edit cookie");
}

void ReturningInputReplacesPreeditAndInvalidatesLateCleanup() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Hold);
  Require(fixture.old_context->pending.size() == 1,
          "retain one host-owned cleanup session for late delivery");
  fixture.old_context->delivery = HostContext::Delivery::Grant;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.old_context, L"\x4f60");
  Require(
      fixture.old_context->text == L"\x4f60" &&
          fixture.composition->end_calls == 1 &&
          WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 0,
      "returning commit must replace ni in its original composition range");
  fixture.old_context->GrantAll();
  Require(fixture.old_context->text == L"\x4f60" &&
              fixture.composition->end_calls == 1 &&
              fixture.old_context->invalid_writes == 0,
          "late cleanup must neither erase committed text nor end it twice");
}

void DroppedCleanupKeepsOwnershipForReturningInput() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Drop);
  Require(WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 1,
          "dropping cleanup must not drop composition ownership");
  fixture.old_context->delivery = HostContext::Delivery::Grant;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.old_context, L"\x4f60");
  Require(
      fixture.old_context->text == L"\x4f60" &&
          fixture.composition->end_calls == 1,
      "a returning commit must still replace preedit after cleanup disposal");
}

void CleanupCannotCrossAReset() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Hold);
  fixture.composition->on_get_range = [&] {
    WeaselTSFTestAccess::ResetCompositionState(fixture.service);
  };
  fixture.old_context->GrantOne();
  Require(fixture.old_context->text == L"ni" && fixture.composition->active &&
              fixture.composition->end_calls == 0,
          "a reset during GetRange must invalidate the old cleanup before it "
          "writes");
}

void PendingReturnCannotBeClearedByAnOlderCleanup() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Hold);
  fixture.next_context->delivery = HostContext::Delivery::Hold;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.next_context, L"F");
  fixture.old_context->delivery = HostContext::Delivery::Grant;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.old_context, L"\x4f60");
  fixture.old_context->GrantOne();
  Require(
      fixture.old_context->text == L"ni" && fixture.composition->active,
      "cleanup must preserve the original range for a queued returning commit");
  fixture.next_context->GrantAll();
  fixture.old_context->GrantAll();
  Require(fixture.old_context->text == L"\x4f60" &&
              fixture.composition->end_calls == 1 &&
              fixture.next_context->text == L"BCDEF",
          "pending returning input must replace preedit exactly once");
}

void DisconnectedContextDoesNotKeepCleanupOwnership() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Disconnected);
  Require(WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 0 &&
              fixture.old_context->invalid_writes == 0,
          "confirmed disconnection must release the unreachable cleanup owner");
}

void MultipleOldContextsKeepTheirOwnCompositionRanges() {
  Fixture fixture;
  fixture.AbandonHandoff(HostContext::Delivery::Drop);
  com_ptr<HostComposition> next_composition;
  next_composition.Attach(
      new HostComposition(fixture.next_context, &fixture.service));
  WeaselTSFTestAccess::SetComposition(fixture.service, fixture.next_context,
                                      next_composition);
  fixture.next_context->delivery = HostContext::Delivery::Drop;
  com_ptr<HostContext> third_context;
  third_context.Attach(new HostContext(L""));
  for (const wchar_t* commit : {L"X", L"Y", L"Z", L"W"})
    WeaselTSFTestAccess::EnqueueContextResponse(fixture.service, third_context,
                                                commit);
  third_context->GrantAll();
  Require(
      WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 2 &&
          third_context->text == L"XYZW",
      "multiple old contexts must not overwrite each other's cleanup owner");
  fixture.old_context->delivery = HostContext::Delivery::Grant;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.old_context, L"\x4f60");
  fixture.next_context->delivery = HostContext::Delivery::Grant;
  WeaselTSFTestAccess::EnqueueContextResponse(fixture.service,
                                              fixture.next_context, L"\x597d");
  Require(
      fixture.old_context->text == L"\x4f60" &&
          fixture.next_context->text == L"\x597d" &&
          fixture.composition->end_calls == 1 &&
          next_composition->end_calls == 1 &&
          WeaselTSFTestAccess::RetainedCompositionCount(fixture.service) == 0,
      "returning commits must replace only their own original preedit");
}

}  // namespace

void RunRetainedCompositionTests() {
  CleanupRequiresTheOriginalWriteLock();
  ReturningInputReplacesPreeditAndInvalidatesLateCleanup();
  DroppedCleanupKeepsOwnershipForReturningInput();
  CleanupCannotCrossAReset();
  PendingReturnCannotBeClearedByAnOlderCleanup();
  DisconnectedContextDoesNotKeepCleanupOwnership();
  MultipleOldContextsKeepTheirOwnCompositionRanges();
  std::puts(
      "[ok] retained composition write locks, returning commits and stale "
      "cleanup");
}
