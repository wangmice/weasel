#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselTSF/ResponseApply.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

using weasel::tsf::BuildResponseApplyPlan;
using weasel::tsf::MakeResponseApplySnapshot;

static_assert(
    [] {
      const auto plan = BuildResponseApplyPlan(false, true, false);
      return !plan.end_existing_composition && !plan.start_composition;
    }(),
    "commit-only response must not create a transient composition");

static_assert(
    [] {
      const auto plan = BuildResponseApplyPlan(false, true, true);
      return !plan.end_existing_composition && plan.start_composition;
    }(),
    "commit plus new preedit starts exactly the replacement composition");

static_assert(
    [] {
      const auto plan = BuildResponseApplyPlan(true, true, true);
      return plan.end_existing_composition && plan.start_composition;
    }(),
    "commit replaces an existing composition when Rime keeps composing");

static_assert(
    [] {
      const auto plan = BuildResponseApplyPlan(true, true, false);
      return plan.end_existing_composition && !plan.start_composition;
    }(),
    "commit ends an existing composition without starting a replacement");

bool ResponseSnapshotsAreIsolated() {
  auto first = std::make_shared<weasel::Context>();
  first->preedit.str = L"first";
  weasel::Status first_status;
  first_status.composing = true;
  weasel::Config first_config;

  auto first_snapshot =
      MakeResponseApplySnapshot(L"commit-a", first, first_status, first_config);

  auto second = std::make_shared<weasel::Context>();
  second->preedit.str = L"second";
  weasel::Status second_status;
  second_status.composing = false;
  weasel::Config second_config;

  // Simulate a later server response replacing the TSF's current response
  // state while the first edit session is still waiting for a document lock.
  auto second_snapshot = MakeResponseApplySnapshot(
      L"commit-b", second, second_status, second_config);

  return first_snapshot.commit == L"commit-a" &&
         first_snapshot.context == first &&
         first_snapshot.context->preedit.str == L"first" &&
         first_snapshot.status.composing &&
         second_snapshot.commit == L"commit-b" &&
         second_snapshot.context == second &&
         second_snapshot.context->preedit.str == L"second" &&
         !second_snapshot.status.composing;
}

struct ResponseApplyRegressionGuard {
  ResponseApplyRegressionGuard() {
    if (!ResponseSnapshotsAreIsolated()) {
      std::fputs("response apply snapshot isolation regression\n", stderr);
      std::abort();
    }
  }
};

ResponseApplyRegressionGuard g_response_apply_regression_guard;

}  // namespace
