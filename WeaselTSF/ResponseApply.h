#pragma once

#include "ResponseApplyState.h"
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
  ApplyProgress progress = ApplyProgress::BeforeCommit;
  unsigned retry_count = 0;
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
    Config config) {
  return {std::move(commit), std::move(context), std::move(status),
          std::move(config), ApplyProgress::BeforeCommit, 0};
}

}  // namespace weasel::tsf
