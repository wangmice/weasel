#pragma once

#include <windows.h>

namespace weasel {

// Allocate process-local IPC session IDs monotonically across maintenance
// cycles. `next_id` is never reset when the active-session container is
// cleared, so a stale client cannot collide with a newly-created session.
// The in-use predicate only matters on DWORD wraparound.
template <typename IsInUse>
DWORD AllocateMonotonicSessionId(DWORD& next_id, IsInUse&& is_in_use) {
  if (next_id == 0)
    next_id = 1;

  const DWORD start = next_id;
  do {
    const DWORD candidate = next_id++;
    if (next_id == 0)
      next_id = 1;
    if (candidate != 0 && !is_in_use(candidate))
      return candidate;
  } while (next_id != start);

  return 0;
}

}  // namespace weasel
