#pragma once

namespace weasel::setup {

// Logical TSF uninstall transaction:
//   1) disable/remove the user's LayoutOrTip entries while profiles exist,
//   2) unregister every architecture,
//   3) if either phase fails, restore registration/profile/layout state.
// Physical DLL deletion is deliberately outside this transaction.
template <typename Ops>
auto RunTsfUninstallTransaction(Ops& ops) -> decltype(ops.remove_layouts()) {
  auto result = ops.remove_layouts();
  if (ops.failed(result)) {
    // A failing InstallLayoutOrTip call may already have changed user state.
    ops.restore_layouts();
    return result;
  }

  result = ops.unregister_services();
  if (ops.failed(result)) {
    // DllUnregisterServer is not atomic: a nonzero regsvr32 result can follow
    // partial COM/TSF mutation. Rebuild registration first, then restore the
    // exact user-facing state captured before the transaction.
    ops.restore_services();
    ops.restore_profiles();
    ops.restore_layouts();
    return result;
  }

  return ops.success();
}

}  // namespace weasel::setup
