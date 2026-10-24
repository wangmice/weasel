#pragma once

#include "ResponseApplyState.h"
#include "ResponseEpoch.h"
#include <WeaselIPCData.h>
#include <memory>
#include <string>
#include <utility>

namespace weasel::tsf {

struct ResponseApplySnapshot {
  std::wstring commit;
  std::shared_ptr<Context> context;
  Status status;
  Config config;
  ResponseIdentity identity;
  ApplyProgress progress = ApplyProgress::BeforeCommit;
  RetryBudget retries;
};

struct ResponseApplyPlan {
  bool end_existing_composition;
  bool start_composition;
};

constexpr ResponseApplyPlan BuildResponseApplyPlan(bool has_composition,
                                                   bool has_commit,
                                                   bool response_composing) {
  return {has_composition && has_commit,
          response_composing && (!has_composition || has_commit)};
}

inline ResponseApplySnapshot MakeResponseApplySnapshot(
    std::wstring commit,
    std::shared_ptr<Context> context,
    Status status,
    Config config,
    ResponseIdentity identity = {}) {
  return {std::move(commit),
          std::move(context),
          std::move(status),
          std::move(config),
          identity,
          ApplyProgress::BeforeCommit,
          {}};
}

}  // namespace weasel::tsf
