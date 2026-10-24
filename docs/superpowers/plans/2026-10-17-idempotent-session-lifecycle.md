# Idempotent IPC Session Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `START_SESSION` idempotent across named-pipe timeout/reconnect and reclaim abandoned sessions when the owning client process exits, without adding work to the healthy key-processing hot path.

**Architecture:** The client generates a stable 128-bit `SessionKey` before `START_SESSION`; the server maps that logical key to the existing `DWORD ipc_id` and Rime session through an explicit `SessionManager`. Named-pipe connections remain transport only. A server-side process tracker derives the authoritative client PID from the pipe handle and supplies a generation-safe `ClientProcessToken` used only for ownership/reaping; normal post-start commands continue to route solely by `DWORD ipc_id`.

**Tech Stack:** C++17, Win32 named pipes, `BCryptGenRandom`, librime, Boost threads/TSS, xmake, MSBuild, PowerShell regression policies.

**Spec:** `docs/superpowers/specs/2026-10-17-idempotent-session-lifecycle-design.md`

## Global Constraints

- Keep `PipeMessage` layout and existing command IDs unchanged in the first implementation.
- Keep healthy `PROCESS_KEY_EVENT` routing on the existing `DWORD ipc_id`; no SessionKey parsing, PID lookup, process-handle checks, new mutexes, or extra round trips on that path.
- Generate new SessionKeys with a Windows system CSPRNG; do not fall back to predictable keys if generation fails.
- Serialize SessionKey in the existing START_SESSION text body as exactly 32 lowercase hexadecimal characters under `session.client_key=`.
- Malformed `session.client_key` is a hard START_SESSION failure (`ipc_id == 0`), not legacy fallback.
- Old clients without `session.client_key` remain supported through the legacy create path.
- New clients remain compatible with old servers because unknown START_SESSION body fields are ignored.
- `g_api_mutex` remains the sole serialization boundary for `RequestHandler`, SessionManager, `SessionStatusMap`, and librime state.
- Never hold `PipeServer::m_workers_mutex` or ClientProcessTracker metadata locks while acquiring `g_api_mutex`.
- `m_next_ipc_session_id` remains process-lifetime monotonic state and is not reset by maintenance/session-map clears.
- Ordinary state-changing IPC timeout remains 3000 ms; maintenance remains 30000 ms; teardown remains 100 ms.

## Review Focus

- START_SESSION response is lost after backend creation: retrying the same SessionKey must return the same logical session and create the backend exactly once.
- `cleanup_stale_sessions()` invalidates the Rime session behind an existing SessionKey: the next Ensure must discard stale indexes and rebuild exactly once under the same key.
- The same numeric PID is reused by a different Windows process: the new process must receive a new `ClientProcessToken` and must not inherit or adopt old sessions.
- One thread-local pipe disconnects while another thread in the same process continues using the session: no logical session may be destroyed because a connection disappeared.
- Server/process-tracker teardown races with a process-exit callback: callback draining must complete before `RequestHandler` can be destroyed, with no lock-order inversion or use-after-free.

---

## File Structure

### New shared/session files

- `include/WeaselSessionKey.h` — `SessionKey` value type, parsing/formatting declarations, hash/ordering helpers.
- `WeaselIPC/WeaselSessionKey.cpp` — CSPRNG generation and fixed 32-hex serialization/parsing.
- `WeaselIPCServer/SessionManager.h` — server-only logical-session identity/lifetime metadata and callback-based create/destroy interface.
- `WeaselIPCServer/SessionManager.cpp` — SessionKey/id/owner indexes and idempotent ensure/remove/clear behavior.
- `WeaselIPCServer/ClientProcessTracker.h` — authoritative PID-to-generation token tracking API and test seam.
- `WeaselIPCServer/ClientProcessTracker.cpp` — peer process handle ownership, generation tokens, process-exit notification draining.

### Existing files to modify

- `WeaselIPC/WeaselClientImpl.h` / `.cpp` — persist SessionKey across transport failure; write it into START_SESSION body; retire it at logical-session boundaries.
- `include/WeaselIPC.h` — evolve RequestHandler session-creation interface without changing `PipeMessage`; keep public client surface source-compatible where possible.
- `include/PipeServer.h` / `WeaselIPCServer/WeaselServerImpl.cpp` / `.h` — attach authoritative peer PID/connection metadata to server dispatch and hand process-exit cleanup into the existing API serialization boundary.
- `include/RimeWithWeasel.h` / `RimeWithWeasel/RimeWithWeasel.cpp` — separate create/ensure/reuse semantics, validate stale Rime backing sessions, and keep Rime/UI initialization exactly-once for newly created sessions.
- `WeaselIPC/WeaselIPC.vcxproj` and `WeaselIPCServer/WeaselIPCServer.vcxproj` (plus `.filters` if present) — register new `.cpp/.h` files for MSBuild. xmake already uses `*.cpp` globs in both directories.
- `test/TestServerCommands/TestServerCommands.cpp` — keyed retry, legacy compatibility, stale rebuild, cross-process rejection, process cleanup, connection independence.
- `test/TestWeaselIPC/*` or the nearest existing low-level IPC test target — SessionKey parse/format/generation tests.
- `.github/workflows/ci.yml` only if a new standalone test target/policy is added; otherwise existing `TestServerCommands`/`TestWeaselIPC` CI coverage is sufficient.

---

### Task 1: SessionKey value type and secure generation

**Files:**
- Create: `include/WeaselSessionKey.h`
- Create: `WeaselIPC/WeaselSessionKey.cpp`
- Modify: `WeaselIPC/WeaselIPC.vcxproj`
- Modify: `WeaselIPC/WeaselIPC.vcxproj.filters` if present
- Test: existing `TestWeaselIPC` source file(s), or add `test/TestWeaselIPC/SessionKeyTest.cpp` if that target uses `*.cpp`

**Interfaces:**
- Produces:
  - `struct weasel::SessionKey { std::array<std::uint8_t, 16> bytes; ... };`
  - `std::optional<SessionKey> ParseSessionKey(std::wstring_view text);`
  - `std::wstring FormatSessionKey(const SessionKey& key);`
  - `std::optional<SessionKey> GenerateSessionKey();`
  - equality plus a deterministic hash/comparator usable by server indexes.
- Consumes: Win32 `BCryptGenRandom(..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)`.

- [ ] **Step 1: Write failing SessionKey tests**

Add tests asserting:

```cpp
FormatSessionKey({00..0f}) == L"000102030405060708090a0b0c0d0e0f";
ParseSessionKey(valid_32_lower_hex) == expected;
ParseSessionKey(L"") == std::nullopt;
ParseSessionKey(L"abc") == std::nullopt;
ParseSessionKey(31_chars) == std::nullopt;
ParseSessionKey(33_chars) == std::nullopt;
ParseSessionKey(non_hex) == std::nullopt;
```

Also generate at least two keys and assert both calls succeed and are not equal. Do not assert statistical properties.

- [ ] **Step 2: Run the focused test and verify RED**

Run the project’s focused `TestWeaselIPC` target (`xmake run TestWeaselIPC` after a debug build if needed).

Expected: compile failure because SessionKey interfaces do not exist yet.

- [ ] **Step 3: Implement SessionKey**

Implement the exact interfaces above. Formatting must always emit 32 lowercase hex characters. Parsing may accept uppercase hex only if the spec is intentionally broadened before implementation; otherwise accept canonical lowercase only and reject malformed input. `GenerateSessionKey()` returns `std::nullopt` on CSPRNG failure and never falls back.

Use `#pragma comment(lib, "bcrypt.lib")` in the implementation if that is the least invasive way to satisfy both xmake/MSBuild linkers; otherwise update both build systems explicitly.

- [ ] **Step 4: Run focused test and both build systems**

Run:

```powershell
xmake run TestWeaselIPC
```

Then at minimum build the MSBuild/xmake targets that consume `WeaselIPC`; no completion claim until both compile paths have fresh evidence.

Expected: PASS / exit 0.

- [ ] **Step 5: Commit**

```text
feat(ipc): add stable session keys

- generate 128-bit logical session identities with the Windows CSPRNG
- serialize keys as fixed lowercase hexadecimal START_SESSION metadata
- reject malformed or insecure session identities
```

---

### Task 2: Standalone SessionManager identity/lifetime indexes

**Files:**
- Create: `WeaselIPCServer/SessionManager.h`
- Create: `WeaselIPCServer/SessionManager.cpp`
- Modify: `WeaselIPCServer/WeaselIPCServer.vcxproj`
- Modify: `WeaselIPCServer/WeaselIPCServer.vcxproj.filters` if present
- Test: `test/TestServerCommands/TestServerCommands.cpp`

**Interfaces:**
- Consumes: `SessionKey` from Task 1; existing monotonic `WeaselSessionId` allocation semantics.
- Produces:

```cpp
struct ClientProcessToken {
  std::uint64_t value = 0;
  friend bool operator==(const ClientProcessToken&, const ClientProcessToken&) = default;
};

struct SessionOwner {
  std::optional<ClientProcessToken> process;
};

class SessionManager {
 public:
  struct EnsureResult {
    WeaselSessionId ipc_id = 0;
    bool created = false;
  };

  using Create = std::function<std::optional<WeaselSessionId>()>;
  using Destroy = std::function<void(WeaselSessionId)>;
  using Validate = std::function<bool(WeaselSessionId)>;

  EnsureResult Ensure(const SessionKey& key,
                      SessionOwner owner,
                      Validate validate,
                      Create create,
                      Destroy destroy);
  bool BindLegacy(WeaselSessionId ipc_id, SessionOwner owner);
  bool Remove(WeaselSessionId ipc_id, Destroy destroy);
  void RemoveClient(ClientProcessToken owner, Destroy destroy);
  void Clear(Destroy destroy);
  std::optional<WeaselSessionId> Find(const SessionKey& key) const;
};
```

The exact container choice is implementation detail; externally observable invariants are not.

- [ ] **Step 1: Write failing SessionManager unit tests in `TestServerCommands`**

Cover:

- first `Ensure(K,P)` calls create once and returns `{id,true}`;
- second `Ensure(K,P)` returns same id with `created=false` and does not call create again;
- `Validate(id)==false` removes/destroys stale id and recreates exactly once under the same key;
- same key with a different non-empty process token is rejected (`ipc_id==0`) without create/destroy;
- an initially unowned keyed entry may be bound to a known owner on a later same-key ensure;
- `Remove(id)` removes id/key/owner indexes and is idempotent;
- `RemoveClient(P)` removes only P’s sessions;
- `Clear()` empties every index;
- legacy bound entries participate in owner cleanup but have no key lookup.

- [ ] **Step 2: Run `xmake run TestServerCommands` and verify RED**

Expected: compile failure for missing `SessionManager`.

- [ ] **Step 3: Implement minimal SessionManager**

No internal mutex: document that all mutation is serialized by `g_api_mutex` when integrated. Ensure stale validation/destroy/recreate happens in one call so the key cannot transiently alias two live sessions.

- [ ] **Step 4: Run focused tests**

```powershell
xmake run TestServerCommands
```

Expected: all existing and new SessionManager tests PASS.

- [ ] **Step 5: Commit**

```text
feat(ipc): add logical session manager

- index keyed sessions independently of named-pipe connections
- make session ensure idempotent and stale-safe
- track optional process ownership for deterministic cleanup
```

---

### Task 3: Client START_SESSION becomes retry-stable

**Files:**
- Modify: `WeaselIPC/WeaselClientImpl.h`
- Modify: `WeaselIPC/WeaselClientImpl.cpp`
- Modify: `include/WeaselIPC.h` only if test access/internal declarations require it
- Test: `test/TestServerCommands/TestServerCommands.cpp`
- Test: low-level client tests in `TestWeaselIPC` if available

**Interfaces:**
- Consumes: `GenerateSessionKey`, `FormatSessionKey`.
- Produces internal client state:

```cpp
std::optional<SessionKey> session_key_;
```

and helper semantics:

```cpp
bool _EnsureSessionKey();          // create once, false on RNG failure
void _RetireSessionKey();          // logical session is over; never reuse old key
```

No new public wire command and no change to `PipeMessage`.

- [ ] **Step 1: Write failing client lifecycle tests**

Use test access to assert:

- first StartSession attempt generates exactly one key and writes `session.client_key=<32hex>` before `.`;
- transport failure resets `session_id` but preserves the same `session_key_`;
- a retry sends the exact same key;
- invalid-session recovery retires the old key before the next logical StartSession;
- explicit EndSession retires the key even if teardown response is ambiguous, preventing the old key from naming a later logical session;
- CSPRNG failure (via test seam/injected generator) causes StartSession to fail without sending an unkeyed request.

- [ ] **Step 2: Run focused tests and verify RED**

Run `TestWeaselIPC` and/or `TestServerCommands` depending on where the seam lives.

- [ ] **Step 3: Implement stable client key state**

Extend `_WriteClientInfo()` to write `session.client_key=` before the terminating `.`. Keep existing `client_app`/`client_type` fields unchanged. `_SendMessage()` transport failure continues resetting only `session_id`; it must not retire the key. Explicit logical end/invalid-session paths retire it.

Do not add SessionKey work to `ProcessKeyEventEx()`.

- [ ] **Step 4: Run client/first-key regressions**

```powershell
.\test\TestFirstKeyWakeupPolicy.ps1
.\test\TestPipeDeadlinePolicy.ps1
xmake run TestWeaselIPC
xmake run TestServerCommands
```

Expected: PASS.

- [ ] **Step 5: Commit**

```text
feat(ipc): keep session identity across reconnects

- persist one client-generated SessionKey across START_SESSION retries
- retire keys only at logical session boundaries
- keep the typing hot path on the existing DWORD session id
```

---

### Task 4: Integrate keyed EnsureSession with RequestHandler/Rime lifecycle

**Files:**
- Modify: `include/WeaselIPC.h`
- Modify: `include/RimeWithWeasel.h`
- Modify: `RimeWithWeasel/RimeWithWeasel.cpp`
- Modify: `WeaselIPCServer/WeaselServerImpl.h`
- Modify: `WeaselIPCServer/WeaselServerImpl.cpp`
- Test: `test/TestServerCommands/TestServerCommands.cpp`

**Interfaces:**
- Consumes: SessionManager from Task 2 and SessionKey parser from Task 1.
- Produces a creation API that distinguishes keyed ensure from legacy creation while keeping normal command routing unchanged. Preferred shape:

```cpp
struct StartSessionRequest {
  std::optional<SessionKey> key;
  std::optional<ClientProcessToken> owner;
  LPWSTR body = nullptr;
};

struct StartSessionResult {
  WeaselSessionId ipc_id = 0;
  bool created = false;
};

virtual StartSessionResult EnsureSession(const StartSessionRequest& request,
                                         EatLine eat = 0);
```

If retaining `AddSession()` as a compatibility shim reduces churn, it may delegate to unkeyed `EnsureSession`; do not duplicate creation logic.

- [ ] **Step 1: Write failing START_SESSION integration tests**

With `SessionHandler`/fake backend verify:

- keyed START_SESSION K returns id A and increments backend-create count once;
- retry K (simulating lost first response) returns A, emits a fresh response body, backend-create count remains one;
- malformed `session.client_key` returns 0 and creates nothing;
- missing key follows legacy behavior and creates a session;
- stale keyed mapping (fake Validate returns false / simulated cleanup epoch invalidation) destroys A, creates B once, and keeps K bound to B;
- second keyed request from same owner/process but another thread/pipe reuses A.

- [ ] **Step 2: Run `xmake run TestServerCommands` and verify RED**

Expected: keyed tests fail because server still calls legacy `AddSession()`.

- [ ] **Step 3: Implement keyed START_SESSION parsing and ensure**

Parse only the exact `session.client_key=` field from the existing body. Distinguish three states: field absent = legacy; field valid = keyed; field present but malformed/duplicate = fail closed.

On reuse, generate a fresh response body from the existing live session; do not re-run destructive creation initialization. On stale validation failure, remove all manager/Rime indexes under `g_api_mutex` before creating the replacement.

- [ ] **Step 4: Run server/session regressions**

```powershell
xmake run TestServerCommands
xmake run TestWeaselIPC
```

Expected: PASS.

- [ ] **Step 5: Commit**

```text
feat(server): make START_SESSION idempotent

- parse client SessionKeys without changing pipe framing
- reuse live keyed sessions across lost responses and reconnects
- rebuild stale Rime backing sessions under the same logical identity
```

---

### Task 5: Attach authoritative client PID to server dispatch

**Files:**
- Modify: `include/PipeServer.h`
- Modify: `WeaselIPCServer/WeaselServerImpl.cpp`
- Test: `test/TestServerCommands/TestServerCommands.cpp`
- Test: `test/TestPipeChannel/TestPipeChannel.cpp` if transport metadata belongs there

**Interfaces:**
- Produces:

```cpp
struct ConnectionContext {
  std::uint64_t worker_id = 0;
  std::optional<DWORD> client_pid;
};

using ServerHandler =
    std::function<void(const ConnectionContext&, PipeMessage, Respond)>;
```

`client_pid` is obtained server-side with `GetNamedPipeClientProcessId(pipe, &pid)` once per accepted worker. Failure yields `std::nullopt`; it never trusts request body PID.

- [ ] **Step 1: Write failing transport-context tests**

Verify:

- private test pipe dispatch exposes a nonzero worker id;
- when PID lookup succeeds, handler sees the real current helper/client PID;
- PID lookup failure is represented as no PID and does not abort request dispatch;
- one connection closing does not trigger session removal (pin the transport/session separation explicitly).

- [ ] **Step 2: Run focused transport/server tests and verify RED**

```powershell
xmake run TestPipeChannel
xmake run TestServerCommands
```

- [ ] **Step 3: Implement ConnectionContext propagation**

Capture PID after server-side pipe acceptance and before worker request loop. Do not call process APIs on every request. Do not take `g_api_mutex` from worker registry code.

- [ ] **Step 4: Run focused tests**

Expected: PASS.

- [ ] **Step 5: Commit**

```text
feat(ipc): attach peer process identity to pipe workers

- query the authoritative client PID once per server connection
- propagate immutable transport context into request dispatch
- keep pipe disconnect independent from logical session lifetime
```

---

### Task 6: Generation-safe client process tracking and exit notification

**Files:**
- Create: `WeaselIPCServer/ClientProcessTracker.h`
- Create: `WeaselIPCServer/ClientProcessTracker.cpp`
- Modify: `WeaselIPCServer/WeaselIPCServer.vcxproj`
- Modify: `WeaselIPCServer/WeaselIPCServer.vcxproj.filters` if present
- Test: `test/TestServerCommands/TestServerCommands.cpp`

**Interfaces:**
- Consumes: authoritative PID from Task 5.
- Produces:

```cpp
class ClientProcessTracker {
 public:
  using ExitCallback = std::function<void(ClientProcessToken)>;

  explicit ClientProcessTracker(ExitCallback on_exit);
  ~ClientProcessTracker();

  std::optional<ClientProcessToken> Resolve(DWORD pid);
  void Stop();  // idempotent; drains callbacks before returning
};
```

The implementation may wrap Win32 wait registration behind an internal interface for deterministic tests. `Resolve` returns the same token for the same still-running process handle and a new token after PID reuse.

- [ ] **Step 1: Write failing tracker tests against a fake wait/process backend**

Verify:

- repeated Resolve(same live process) returns same token;
- simulated process exit produces exactly one exit callback;
- same numeric PID after old handle is signaled gets a different token;
- `OpenProcess(SYNCHRONIZE)` failure returns `std::nullopt` without poisoning later resolutions;
- `Stop()` prevents new callbacks and waits for in-flight callback completion;
- callback execution never occurs while the tracker metadata mutex is held (test with a reentrant/fake callback that queries tracker state or via explicit lock-order instrumentation).

- [ ] **Step 2: Run `xmake run TestServerCommands` and verify RED**

- [ ] **Step 3: Implement tracker**

Use authoritative process handles with `SYNCHRONIZE`. Prefer a lifetime-safe Windows wait primitive whose callbacks can be synchronously drained during `Stop()` (for example threadpool wait objects) over detached threads. Callback code must only enqueue/hand off the token; it must not directly touch `RequestHandler` while holding tracker state.

- [ ] **Step 4: Run focused tests plus one real-process Windows integration test**

Spawn a short-lived helper process, resolve its PID, let it exit, and verify one token-exit notification. Keep the fake-backend tests as the exhaustive race suite.

- [ ] **Step 5: Commit**

```text
feat(server): track client process generations

- bind authoritative pipe PIDs to generation-safe process tokens
- notify session cleanup on real client process exit
- drain process wait callbacks before server teardown
```

---

### Task 7: Wire process ownership into session cleanup and maintenance

**Files:**
- Modify: `WeaselIPCServer/WeaselServerImpl.h`
- Modify: `WeaselIPCServer/WeaselServerImpl.cpp`
- Modify: `RimeWithWeasel/RimeWithWeasel.cpp`
- Modify: `include/RimeWithWeasel.h`
- Test: `test/TestServerCommands/TestServerCommands.cpp`

**Interfaces:**
- Consumes: `ConnectionContext.client_pid`, `ClientProcessTracker::Resolve`, `SessionManager::RemoveClient/Clear`.
- Produces: deterministic owner cleanup under `g_api_mutex` with teardown ordering:

```text
stop accepting/drain pipe workers
stop/drain process tracker callbacks
stop/join session-cleanup handoff
only then allow RequestHandler/UI destruction
```

- [ ] **Step 1: Write failing ownership/teardown tests**

Pin every Review Focus condition:

- two sessions owned by token P and one by Q; P exit removes exactly P’s two sessions;
- a pipe/thread disconnect alone removes none;
- PID reuse token Q cannot reuse/adopt P’s SessionKey;
- maintenance clears SessionManager indexes and Rime/session-status entries atomically while preserving monotonic IPC id high-water mark;
- process-exit cleanup racing maintenance is idempotent (destroy callback for each session at most once);
- ServerImpl teardown drains queued exit cleanup before fake RequestHandler destruction; assert no callback after destruction flag.

- [ ] **Step 2: Run `xmake run TestServerCommands` and verify RED**

- [ ] **Step 3: Integrate owner resolution and cleanup handoff**

Resolve PID to token outside `g_api_mutex` if tracker internals may block on process APIs; then enter `g_api_mutex` for SessionManager/RequestHandler mutation. Process-exit callback releases tracker locks first, then schedules cleanup that acquires `g_api_mutex`. Never acquire `m_workers_mutex` in this path.

When PID/process-handle lookup fails, pass an empty owner: keyed idempotency still works, only process-exit reaping is unavailable.

- [ ] **Step 4: Run server regressions repeatedly**

```powershell
1..20 | ForEach-Object { xmake run TestServerCommands; if ($LASTEXITCODE) { throw "iteration $_ failed" } }
```

Then run `TestPipeChannel` once. Expected: zero failures/deadlocks/timeouts.

- [ ] **Step 5: Commit**

```text
fix(server): reap sessions with their client process

- remove all process-owned sessions after authoritative client exit
- keep pipe disconnects transport-only and thread-safe
- serialize maintenance, exit cleanup and teardown without lock inversion
```

---

### Task 8: Mixed-version compatibility, policy checks, and full verification

**Files:**
- Modify tests/policies only as required by real implementation changes.
- Modify `.github/workflows/ci.yml` only if new targets are not already invoked.
- No production refactor in this task except fixes proven necessary by failing verification.

**Interfaces:**
- Consumes: completed Tasks 1–7.
- Produces: merge-ready evidence, not new architecture.

- [ ] **Step 1: Add/confirm compatibility tests**

Cover:

- new client body with `session.client_key` is accepted by a legacy-body parser that ignores unknown fields;
- missing key on new server uses legacy create path;
- malformed/duplicate key is rejected instead of treated as legacy;
- keyed retry on new server is idempotent;
- normal key traffic after StartSession contains no SessionKey and uses existing `DWORD ipc_id` only.

- [ ] **Step 2: Run policy scripts**

```powershell
.\test\TestFirstKeyWakeupPolicy.ps1
.\test\TestPipeDeadlinePolicy.ps1
.\test\TestRuntimeHardeningPolicy.ps1
```

Expected: PASS.

- [ ] **Step 3: Build and run focused native tests**

```powershell
xmake f -c -a x64 -m debug --vs=2022 --vs_toolset=14.3 -y
xmake -y
xmake run TestWeaselIPC
xmake run TestPipeChannel
xmake run TestServerCommands
```

Expected: all PASS.

- [ ] **Step 4: Run the project’s complete xmake regression set used by CI**

Use the same target list as `.github/workflows/ci.yml`, including `TestWeaselSetup`, to catch static-library/link/build fallout from new source files.

Expected: all PASS.

- [ ] **Step 5: Verify MSBuild path**

Run the equivalent project build path used by CI (`build.bat ...` / solution build for the supported local architecture) so `.vcxproj` source registration and `bcrypt` linkage are proven independently of xmake.

Expected: exit 0.

- [ ] **Step 6: Inspect final diff for hot-path and lock regressions**

Review `master...ai/review` and explicitly confirm:

- `ProcessKeyEventEx`/server key dispatch performs no SessionKey parsing or process lookup;
- no tracker/worker mutex is held across `g_api_mutex` acquisition;
- SessionManager mutation is under `g_api_mutex` only;
- server teardown orders tracker callback drain before handler destruction;
- no accidental wire-layout/command-number change.

- [ ] **Step 7: Final commit for test/policy-only adjustments if needed**

```text
test(ipc): cover idempotent session lifecycle

- verify mixed-version START_SESSION compatibility and malformed-key rejection
- exercise client-exit cleanup, PID reuse and stale-session rebuilds
- keep session identity work off the healthy key-processing path
```
