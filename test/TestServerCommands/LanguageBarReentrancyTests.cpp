// Exercise the real language-bar update and Deactivate paths using private
// in-process COM objects. No installed TIP or live keyboard compartment is
// used.
#include "../../WeaselTSF/stdafx.h"
#include "TestWeaselTSFPrivateAccess.h"

#include <cstdio>
#include <functional>
#include <utility>

namespace {

int failures = 0;

void Check(bool ok, const char* message) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", message);
  if (!ok)
    ++failures;
}

// One COM identity implements just the interfaces the production DWORD
// accessors need. Unsupported teardown interfaces fail normally during
// Deactivate.
class ReentrantThreadManager : public ITfThreadMgr,
                               public ITfCompartmentMgr,
                               public ITfCompartment {
 public:
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfThreadMgr)
      *out = static_cast<ITfThreadMgr*>(this);
    else if (iid == IID_ITfCompartmentMgr)
      *out = static_cast<ITfCompartmentMgr*>(this);
    else if (iid == IID_ITfCompartment)
      *out = static_cast<ITfCompartment*>(this);
    else
      return E_NOINTERFACE;
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

  STDMETHODIMP Activate(TfClientId*) override { return E_NOTIMPL; }
  STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetFocus(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  STDMETHODIMP AssociateFocus(HWND,
                              ITfDocumentMgr*,
                              ITfDocumentMgr**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP GetCompartment(REFGUID, ITfCompartment** out) override {
    *out = static_cast<ITfCompartment*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHODIMP ClearCompartment(TfClientId, REFGUID) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP EnumCompartments(IEnumGUID**) override { return E_NOTIMPL; }

  STDMETHODIMP GetValue(VARIANT* out) override {
    out->vt = VT_I4;
    out->lVal = flags;
    auto callback = std::move(on_read);
    if (callback)
      callback();
    return read_result;
  }
  STDMETHODIMP SetValue(TfClientId, const VARIANT* value) override {
    flags = value->lVal;
    ++writes;
    auto callback = std::move(on_write);
    if (callback)
      callback();
    return S_OK;
  }

  DWORD flags = TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_ROMAN;
  HRESULT read_result = S_OK;
  unsigned writes = 0;
  std::function<void()> on_read;
  std::function<void()> on_write;

 private:
  ULONG refs_ = 1;
};

class CountingLanguageBarSink : public ITfLangBarItemSink {
 public:
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfLangBarItemSink)
      return E_NOINTERFACE;
    *out = static_cast<ITfLangBarItemSink*>(this);
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
  STDMETHODIMP OnUpdate(DWORD) override {
    ++updates;
    auto callback = std::move(on_update);
    if (callback)
      callback();
    return S_OK;
  }

  unsigned updates = 0;
  std::function<void()> on_update;

 private:
  ULONG refs_ = 1;
};

com_ptr<CLangBarItemButton> MakeButton(WeaselTSF& tsf,
                                       CountingLanguageBarSink* sink) {
  com_ptr<CLangBarItemButton> button;
  button.Attach(
      new CLangBarItemButton(&tsf, GUID_LBI_INPUTMODE, weasel::UIStyle{}));
  DWORD cookie = 0;
  Check(button->AdviseSink(IID_ITfLangBarItemSink, sink, &cookie) == S_OK,
        "language-bar fixture advises a private sink");
  return button;
}

void TestNormalUpdate() {
  WeaselTSF tsf;
  com_ptr<ReentrantThreadManager> manager;
  manager.Attach(new ReentrantThreadManager);
  com_ptr<CountingLanguageBarSink> sink;
  sink.Attach(new CountingLanguageBarSink);
  auto button = MakeButton(tsf, sink);
  WeaselTSFTestAccess::BindLanguageBar(tsf, manager, button);

  weasel::Status status;
  status.ascii_mode = true;
  status.full_shape = true;
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(manager->flags ==
                (TF_CONVERSIONMODE_ROMAN | TF_CONVERSIONMODE_FULLSHAPE) &&
            manager->writes == 1 && sink->updates == 1,
        "language-bar normal update preserves unrelated conversion bits");
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(manager->writes == 1 && sink->updates == 1,
        "unchanged language-bar state does not repeat writes or notifications");
  tsf.Deactivate();
}

void TestReplacementDuringCompartmentCall(bool during_write, bool failed_read) {
  WeaselTSF tsf;
  com_ptr<ReentrantThreadManager> manager;
  manager.Attach(new ReentrantThreadManager);
  com_ptr<CountingLanguageBarSink> old_sink;
  old_sink.Attach(new CountingLanguageBarSink);
  com_ptr<CountingLanguageBarSink> new_sink;
  new_sink.Attach(new CountingLanguageBarSink);
  auto old_button = MakeButton(tsf, old_sink);
  auto new_button = MakeButton(tsf, new_sink);
  WeaselTSFTestAccess::BindLanguageBar(tsf, manager, old_button);
  manager->read_result = failed_read ? E_FAIL : S_OK;
  unsigned reentries = 0;
  auto replace = [&] {
    ++reentries;
    tsf.Deactivate();
    // Model a new language-bar registration on the same manager. Reusing the
    // manager deliberately prevents a thread-manager-only guard from passing.
    WeaselTSFTestAccess::BindLanguageBar(tsf, manager, new_button);
  };
  if (during_write)
    manager->on_write = replace;
  else
    manager->on_read = replace;

  weasel::Status status;
  status.ascii_mode = true;
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(reentries == 1 && old_sink->updates == 0 && new_sink->updates == 0,
        "reentrant replacement receives no stale language-bar update");
  Check(manager->writes == (during_write ? 1u : 0u),
        "read-side deactivation prevents a stale compartment write");
  tsf.Deactivate();
}

void TestCandidateRenewalDuringCompartmentCall(bool during_write,
                                               bool failed_read) {
  WeaselTSF tsf;
  com_ptr<ReentrantThreadManager> manager;
  manager.Attach(new ReentrantThreadManager);
  com_ptr<CountingLanguageBarSink> sink;
  sink.Attach(new CountingLanguageBarSink);
  auto button = MakeButton(tsf, sink);
  WeaselTSFTestAccess::BindLanguageBar(tsf, manager, button);
  auto old_candidate = WeaselTSFTestAccess::Candidate(tsf);
  old_candidate->style().current_ascii_icon = L"private-before-renewal.ico";
  manager->read_result = failed_read ? E_FAIL : S_OK;

  weasel::Status status;
  status.ascii_mode = true;
  status.full_shape = true;
  unsigned reentries = 0;
  auto renew = [&] {
    ++reentries;
    // Only the candidate changes: the same language-bar registration must
    // still receive this update. This is a private injected COM callback,
    // not a claim that a real TSF host delivers renewal at this boundary.
    WeaselTSFTestAccess::RenewCandidateUI(tsf, status);
    auto candidate = WeaselTSFTestAccess::Candidate(tsf);
    candidate->style().current_ascii_icon = L"private-after-renewal.ico";
  };
  if (during_write)
    manager->on_write = renew;
  else
    manager->on_read = renew;

  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  auto candidate = WeaselTSFTestAccess::Candidate(tsf);
  Check(reentries == 1 && candidate.p != old_candidate.p &&
            candidate->HasOwner() && !old_candidate->HasOwner(),
        "language-bar fixture executes a real candidate-only renewal");
  Check(sink->updates == 1,
        "candidate-only renewal preserves the live language-bar update");
  const DWORD expected_flags =
      failed_read ? (TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_ROMAN)
                  : (TF_CONVERSIONMODE_ROMAN | TF_CONVERSIONMODE_FULLSHAPE);
  Check(manager->flags == expected_flags &&
            manager->writes == (failed_read ? 0u : 1u),
        "candidate-only renewal preserves conversion writes and read failures");

  // Reading the style from a pinned, retired candidate would publish the old
  // icon and cause an extra notification on this otherwise identical update.
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(
      sink->updates == 1 && manager->flags == expected_flags &&
          manager->writes == (failed_read ? 0u : 1u),
      "candidate renewal publishes the current style without a repair update");
  tsf.Deactivate();
}

void TestDeactivationDuringCompartmentCall(bool during_write) {
  WeaselTSF tsf;
  com_ptr<ReentrantThreadManager> manager;
  manager.Attach(new ReentrantThreadManager);
  com_ptr<CountingLanguageBarSink> sink;
  sink.Attach(new CountingLanguageBarSink);
  auto button = MakeButton(tsf, sink);
  WeaselTSFTestAccess::BindLanguageBar(tsf, manager, button);
  unsigned reentries = 0;
  auto deactivate = [&] {
    ++reentries;
    tsf.Deactivate();
  };
  if (during_write)
    manager->on_write = deactivate;
  else
    manager->on_read = deactivate;

  weasel::Status status;
  status.ascii_mode = true;
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(reentries == 1 && sink->updates == 0,
        "language-bar update returns safely after reentrant deactivation");
  Check(manager->writes == (during_write ? 1u : 0u),
        "deactivated language bar performs no further compartment writes");
}

void TestDeactivationDuringNotification() {
  WeaselTSF tsf;
  com_ptr<ReentrantThreadManager> manager;
  manager.Attach(new ReentrantThreadManager);
  com_ptr<CountingLanguageBarSink> sink;
  sink.Attach(new CountingLanguageBarSink);
  // Leave the member as the only button owner while the notification runs.
  // Production must hold its own strong reference across that external call.
  auto button = MakeButton(tsf, sink);
  WeaselTSFTestAccess::BindLanguageBar(tsf, manager, button);
  CLangBarItemButton* raw_button = button;
  button.Release();
  sink->on_update = [&] {
    const ULONG refs = raw_button->AddRef();
    raw_button->Release();
    Check(refs >= 3,
          "language-bar notification retains an owner beyond the TIP member");
    tsf.Deactivate();
  };
  weasel::Status status;
  status.ascii_mode = true;
  WeaselTSFTestAccess::UpdateLanguageBar(tsf, status);
  Check(sink->updates == 1,
        "language-bar notification permits teardown of the member owner");
}

}  // namespace

int RunLanguageBarReentrancyTests() {
  failures = 0;
  TestNormalUpdate();
  TestReplacementDuringCompartmentCall(false, false);
  TestReplacementDuringCompartmentCall(false, true);
  TestReplacementDuringCompartmentCall(true, false);
  TestCandidateRenewalDuringCompartmentCall(false, false);
  TestCandidateRenewalDuringCompartmentCall(false, true);
  TestCandidateRenewalDuringCompartmentCall(true, false);
  // Keep the unfixed baseline diagnostic bounded: replacement assertions fail
  // first, rather than deliberately dereferencing a null member afterwards.
  if (!failures) {
    TestDeactivationDuringCompartmentCall(false);
    TestDeactivationDuringCompartmentCall(true);
    TestDeactivationDuringNotification();
  }
  return failures;
}
