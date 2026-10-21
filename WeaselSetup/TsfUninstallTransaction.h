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
    ops.restore_default_profiles();
    ops.restore_profiles();
    ops.restore_layouts();
    return result;
  }

  return ops.success();
}

template <typename Ops>
auto RunTsfUninstallForwardRecovery(Ops& ops)
    -> decltype(ops.unregister_services()) {
  // LayoutOrTip state may already be partially gone and the prior rollback
  // snapshot no longer exists. Converge forward, but do not unregister the
  // service or commit FileCleanup until every residual layout is confirmed gone.
  auto result = ops.remove_layouts_best_effort();
  if (ops.failed(result))
    return result;

  result = ops.unregister_services();
  if (ops.failed(result))
    return result;

  return ops.mark_file_cleanup();
}

}  // namespace weasel::setup
