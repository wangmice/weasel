#pragma once

namespace weasel::setup {

template <typename Result>
constexpr Result ResultFromBool(bool ok,
                                Result success,
                                Result failure) noexcept {
  return ok ? success : failure;
}

template <typename Ops>
auto RunProfileSwitchTransaction(Ops& ops) -> decltype(ops.enable_new()) {
  auto rollback_profiles = [&ops]() {
    ops.enable_old();
    ops.disable_new();
  };
  auto rollback_layouts_and_profiles = [&ops]() {
    ops.enable_old();
    ops.install_old_layout();
    ops.remove_new_layout();
    ops.disable_new();
  };

  auto result = ops.enable_new();
  if (ops.failed(result)) {
    rollback_profiles();
    return result;
  }

  result = ops.disable_old();
  if (ops.failed(result)) {
    rollback_profiles();
    return result;
  }

  result = ops.remove_old_layout();
  if (ops.failed(result)) {
    ops.enable_old();
    ops.install_old_layout();
    ops.disable_new();
    return result;
  }

  result = ops.install_new_layout();
  if (ops.failed(result)) {
    rollback_layouts_and_profiles();
    return result;
  }

  return ops.success();
}

}  // namespace weasel::setup
