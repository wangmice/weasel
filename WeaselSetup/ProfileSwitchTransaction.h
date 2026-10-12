#pragma once

namespace weasel::setup {

struct LayoutState {
  bool enabled = false;
  bool is_default = false;
};

enum class LayoutRestoreAction { Uninstall, Install, InstallDefault };

constexpr LayoutRestoreAction GetLayoutRestoreAction(
    const LayoutState& state) noexcept {
  if (!state.enabled)
    return LayoutRestoreAction::Uninstall;
  return state.is_default ? LayoutRestoreAction::InstallDefault
                          : LayoutRestoreAction::Install;
}

template <typename Result>
constexpr Result ResultFromBool(bool ok,
                                Result success,
                                Result failure) noexcept {
  return ok ? success : failure;
}

template <typename Ops>
auto RunProfileSwitchTransaction(Ops& ops) -> decltype(ops.enable_new()) {
  auto result = ops.enable_new();
  if (ops.failed(result)) {
    ops.restore_new_profile();
    return result;
  }

  result = ops.disable_old();
  if (ops.failed(result)) {
    ops.restore_old_profile();
    ops.restore_new_profile();
    return result;
  }

  result = ops.remove_old_layout();
  if (ops.failed(result)) {
    ops.restore_old_layout();
    ops.restore_old_profile();
    ops.restore_new_profile();
    return result;
  }

  result = ops.install_new_layout();
  if (ops.failed(result)) {
    ops.restore_new_layout();
    ops.restore_old_layout();
    ops.restore_old_profile();
    ops.restore_new_profile();
    return result;
  }

  return ops.success();
}

}  // namespace weasel::setup
