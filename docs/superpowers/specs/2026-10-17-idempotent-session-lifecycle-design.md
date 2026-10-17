# Idempotent IPC Session Lifecycle Design

Date: 2026-10-17
Branch: `ai/review`
Status: Proposed

## 1. Problem

Weasel currently creates a logical input session with a request/response exchange:

1. the client sends `WEASEL_IPC_START_SESSION`;
2. the server calls `RequestHandler::AddSession()`;
3. librime creates a `RimeSessionId`;
4. Weasel allocates a server-side `DWORD` IPC session id;
5. the server returns that id to the client.

The session identity is therefore created *inside* the server before the client knows it. If the request is delivered and the server creates the session but the response is lost or the client times out, the client resets its local `session_id` to zero while the server retains the newly created session. A later retry can create another session. Repetition can accumulate unreachable sessions.

Increasing the transaction timeout only lowers the probability of this race; it does not remove it.

The transport cannot own the logical session either. `PipeChannel` keeps pipe handles in thread-local storage, while one `ClientImpl` owns a logical `session_id` that can be used across those per-thread pipe connections. Tying a session to the connection that created it would therefore destroy valid sessions when one thread disconnects.

## 2. Goals

The design must:

- make `START_SESSION` idempotent across transport timeout and reconnect;
- ensure a client can retry after a lost response without creating a second Rime session;
- keep logical session lifetime independent from individual named-pipe connections;
- bound abandoned session lifetime to the owning client process rather than to a pipe;
- preserve existing `DWORD ipc_id` command routing for normal commands in the first implementation;
- preserve mixed-version upgrade compatibility as far as the current protocol permits;
- keep the healthy key-processing hot path free of new locks, process checks, SessionKey parsing, or extra pipe round trips;
- keep `g_api_mutex` as the single serialization boundary for `RequestHandler`/librime state;
- avoid timer-based leases, heartbeat traffic, and arbitrary TTL guesses.

## 3. Non-goals

The first implementation will not:

- replace every `DWORD ipc_id` in the wire protocol with a 128-bit token;
- add a general IPC v2 framing protocol;
- add per-request acknowledgement IDs for every command;
- change key exactly-once semantics already provided by `PipeTransactionError`;
- make a logical session survive a WeaselServer process restart;
- preserve an in-progress composition after the owning client process exits;
- make an old client talking to an old server idempotent during a mixed-version upgrade window.

## 4. Chosen Architecture

### 4.1 Transport is not session identity

Named-pipe connections remain transient transport objects. A connection can appear or disappear without directly creating or destroying an established logical session.

`PipeServer` attaches immutable transport metadata to a request, such as an authoritative client-process identity, but the transport layer does not own the Rime session.

### 4.2 Client-generated SessionKey

Each new logical client session is assigned a random 128-bit `SessionKey` *before* the first `START_SESSION` request is sent.

Conceptually:

```cpp
struct SessionKey {
  std::array<std::byte, 16> bytes;
};
```

The client serializes it as a fixed lowercase hexadecimal string in the existing START_SESSION body:

```text
session.client_key=9f613a8b39bb49bf8f9866af99855d92
session.client_app=example.exe
session.client_type=tsf
.
```

The first implementation does not change `PipeMessage` size or the scalar response. Existing body framing already supports additional fields.

The key is generated once for a logical `StartSession` attempt and retained across START_SESSION transport failures and reconnect retries. A lost START_SESSION response therefore does not cause the next retry to invent a new identity.

Recommended generator: `BCryptGenRandom(..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)`. Generation occurs only at session creation, never on the typing hot path.

A SessionKey is never intentionally reused for a *different* logical session. Once the client retires a key, a later logical session receives a new key.

### 4.3 SessionManager

Introduce an explicit server-side lifecycle component rather than growing more identity indexes directly inside `ServerImpl`:

```cpp
class SessionManager {
 public:
  struct EnsureResult {
    WeaselSessionId ipc_id = 0;
    bool created = false;
  };

  EnsureResult EnsureSession(const SessionKey& key,
                             std::optional<ClientProcessToken> owner,
                             SessionValidateCallback validate,
                             SessionCreateCallback create,
                             SessionDestroyCallback destroy);
  bool RemoveSession(WeaselSessionId ipc_id, SessionDestroyCallback destroy);
  void RemoveClient(ClientProcessToken owner, SessionDestroyCallback destroy);
  void Clear(SessionDestroyCallback destroy);

  std::optional<WeaselSessionId> FindByKey(const SessionKey& key) const;
};
```

`SessionManager` owns logical identity and lifetime metadata. `RimeWithWeaselHandler` remains responsible for Rime-specific creation, schema/style setup, response generation, UI state and key processing.

A keyed session entry contains at least:

```cpp
struct SessionEntry {
  SessionKey key;
  WeaselSessionId ipc_id;
  std::optional<ClientProcessToken> owner;
};
```

The existing `SessionStatusMap` remains the Rime/UI state table in the first implementation. The manager does not move unrelated `SessionStatus` data merely for aesthetic completeness; it owns only identity/lifetime policy needed for correctness.

### 4.4 Idempotent EnsureSession

For a keyed client:

```text
START_SESSION(key=K)
        |
        v
SessionManager.FindByKey(K)
   |                 |
 exists           missing
   |                 |
 validate Rime      create one Rime session
 session            allocate ipc_id
   |                 |
 valid / stale      register K -> ipc_id
   |                 |
 reuse / rebuild ---+
        |
        v
 return ipc_id + fresh response body
```

The critical property is:

```text
Ensure(K); Ensure(K); Ensure(K) == at most one live logical session for K
```

If K already maps to a *valid* Rime session, the server does not call `rime_api->create_session()` again. It returns the same `ipc_id` and emits a fresh normal session/status response.

If K maps to an IPC entry whose underlying Rime session was reclaimed by `cleanup_stale_sessions()` or otherwise fails validation, `EnsureSession` must, while still under `g_api_mutex`:

1. remove the stale key/id indexes;
2. destroy/erase any remaining stale handler state idempotently;
3. create one replacement Rime session;
4. bind the *same SessionKey K* to the new `ipc_id`;
5. return the replacement.

This prevents a stale key index from resurrecting an invalid Rime handle while retaining idempotent retry semantics.

### 4.5 Authoritative client-process ownership

Each accepted server-side pipe worker queries the peer PID from the server pipe handle with `GetNamedPipeClientProcessId`. The PID is supplied by the kernel, not by request text.

PID alone is not a safe long-lived identity because Windows can reuse process IDs. A `ClientProcessTracker` therefore attempts to open the process with `SYNCHRONIZE` and allocates a process-generation token:

```cpp
struct ClientProcessToken {
  uint64_t value;
};

struct ClientProcessEntry {
  ClientProcessToken token;
  DWORD pid;
  HANDLE process;
};
```

Connections from the same still-running process resolve to the same token. If the old process handle has become signaled and Windows later reuses its PID, the tracker creates a new token.

Process ownership is deliberately **optional** for session creation. Failure of `GetNamedPipeClientProcessId` or `OpenProcess(SYNCHRONIZE)` must not make START_SESSION unusable. In that case:

- SessionKey idempotency still works;
- the entry stores no process token;
- process-exit reaping is unavailable for that entry;
- explicit END_SESSION, maintenance/finalize, or server exit remain cleanup boundaries.

The SessionKey itself is not an authentication token; failure to acquire process ownership is therefore a cleanup degradation, not an authorization failure.

### 4.6 Process-exit cleanup

A session is owned by a *client process* when an authoritative process token is available, never by a particular pipe connection.

When the tracked process handle becomes signaled, the tracker schedules cleanup of that process token. Cleanup enters the existing `g_api_mutex` serialization boundary and removes every session owned by that token.

No pipe disconnect callback destroys an established logical session.

Lifecycle:

```text
pipe timeout / reconnect     -> no session destruction
thread exits                 -> no session destruction
normal END_SESSION           -> remove one logical session
client process exits/crashes -> remove all token-owned sessions
maintenance/finalize         -> remove all sessions globally
server exits                 -> process teardown owns all remaining state
```

The process wait mechanism must not call `RequestHandler` from a raw wait callback while server teardown may be in progress. The implementation should hand process-exit notifications into a tracker-owned queue/event or equivalent serialized dispatcher that is drained before `ServerImpl`/`RequestHandler` destruction.

### 4.7 Legacy clients

Old clients do not send `session.client_key`.

For a truly absent key field the new server preserves legacy behavior:

- create a normal session;
- allocate a monotonic `ipc_id`;
- mark it as legacy/unkeyed;
- associate it with the authoritative client-process token when available;
- clean it on explicit END_SESSION, process exit, maintenance or server shutdown.

Legacy START_SESSION cannot be made idempotent without changing the old client. Process ownership still bounds its lifetime when tracking is available.

A malformed *present* `session.client_key` is not considered legacy; it fails START_SESSION with id 0 and creates nothing.

### 4.8 New client with old server

A new client may connect to an already-loaded old WeaselServer during an upgrade.

The new client places `session.client_key` in the existing text body. Existing `_ReadClientInfo()` ignores fields it does not recognize, so the old server continues to create a normal session and returns a normal `DWORD ipc_id`.

Idempotent START retry is unavailable until the new server is running, but compatibility is maintained without changing pipe framing or command IDs.

No capability handshake is required for the first implementation.

## 5. SessionKey State on the Client

`ClientImpl` keeps two separate concepts:

```cpp
std::optional<SessionKey> session_key_;  // logical identity
UINT session_id;                         // current server routing handle
```

Rules:

1. `StartSession()` generates a key only when beginning a new logical session and no current key exists.
2. A START_SESSION transport failure resets `session_id` to zero but retains `session_key_` so the next START_SESSION retries the same identity.
3. A successful retry may receive either the original `ipc_id` or a replacement `ipc_id` if the old Rime session was independently reclaimed; both represent K's current logical server instance.
4. An invalid-session response retires the old key locally. The next logical recovery generates a fresh key because the server has explicitly said the old routing instance no longer exists.
5. Explicit `EndSession()` retires the local key regardless of transport outcome. A key that the client has retired is never reused for a new logical session.
6. If END_SESSION is delivered, the server removes the entry immediately.
7. If END_SESSION is definitely not delivered or is ambiguous, the old entry may remain dormant, but it cannot be confused with the client's next logical session because that next session uses a fresh key. When process ownership is available, process exit provides the final deterministic cleanup bound.

This design intentionally does **not** require `EndSession()` to infer transport certainty from the current `_SendMessage()` API. Improving END_SESSION delivery reporting can be done separately; correctness here relies on key retirement plus process ownership rather than key reuse.

## 6. Server Request Flow

The server worker derives immutable transport context once per connected pipe:

```cpp
struct ConnectionContext {
  WorkerId worker_id;
  std::optional<ClientProcessToken> client_process;
};
```

The internal handler becomes conceptually:

```cpp
using ServerHandler =
    std::function<void(const ConnectionContext&, PipeMessage, Respond)>;
```

This is an internal C++ interface only; no wire-format change is implied.

For `START_SESSION`:

1. parse optional `session.client_key` from the request body;
2. take the optional `ClientProcessToken` from `ConnectionContext`;
3. under `g_api_mutex`, run keyed ensure or legacy create;
4. on keyed reuse, validate the existing Rime session before returning it;
5. if newly created/rebuilt, perform current Rime/session-style initialization exactly once for that concrete Rime session;
6. if validly reused, do not repeat destructive initialization;
7. emit a fresh response body and return the current `ipc_id`.

For all other requests, normal `ipc_id` routing remains unchanged.

## 7. Locking and Concurrency

### 7.1 Required lock order

The implementation must preserve this ordering rule:

```text
PipeServer worker ownership locks
    must never be held while acquiring
ClientProcessTracker metadata lock or g_api_mutex
```

`m_workers_mutex` continues to protect only worker handles/threads.

`g_api_mutex` remains the outer serialization point for every operation that touches `RequestHandler`, `SessionManager`, `SessionStatusMap` or librime state.

`SessionManager` therefore needs no additional mutex on normal request paths when accessed under `g_api_mutex`.

The process tracker has its own metadata lock, but a process-exit notification must release that lock before requesting session cleanup under `g_api_mutex`.

No code path may acquire `m_workers_mutex` from inside `g_api_mutex` merely to discover process ownership; the immutable `ConnectionContext` must already contain it.

### 7.2 No typing-hot-path cost

After START_SESSION succeeds, PROCESS_KEY continues to carry only the existing `DWORD ipc_id`.

No SessionKey parsing, process-handle query, tracker lock, process-liveness check, or SessionKey-map lookup is added to healthy key processing.

Additional work is limited to:

- session creation/recovery;
- explicit end-session;
- client-process exit;
- maintenance/finalize.

## 8. Maintenance, cleanup_stale_sessions, and Teardown

### 8.1 Maintenance/finalize

`StartMaintenance`, `EndMaintenance`, handler `Finalize`, and server teardown are global session invalidation boundaries.

They must update all indexes transactionally under `g_api_mutex`:

```text
for each live session:
    destroy Rime session if still valid
clear SessionStatusMap
clear SessionKey -> ipc_id index
clear ipc_id -> SessionEntry index
clear owner -> session index
reset active-session state
```

The monotonic `m_next_ipc_session_id` high-water mark remains process-lifetime state and is not reset merely because maintenance clears active sessions.

### 8.2 librime stale-session cleanup

`cleanup_stale_sessions()` can invalidate an underlying Rime session without immediately deleting the Weasel IPC mapping. Existing epoch validation may keep that cost off the healthy key path.

The new SessionManager must not assume `key -> ipc_id` implies a live Rime session. `EnsureSession` validation described in section 4.4 is mandatory whenever a keyed START retry encounters an existing mapping.

This preserves the current lazy validation strategy for healthy keys while making START retry self-healing.

### 8.3 Process tracker entries

Tracker entries contain process-lifetime metadata, not Rime state. They may remain while a client process is alive even when it currently owns no sessions. Removing empty tracker entries is an optimization, provided reconnecting pipes for the same still-running process cannot be mistaken for a reused PID generation.

## 9. Error Handling

### SessionKey generation failure

If secure SessionKey generation fails, a new client fails session start rather than silently falling back to a predictable or unkeyed key. Legacy unkeyed mode exists only for genuinely old clients.

### Malformed key

A malformed present `session.client_key` returns session id 0 and creates nothing.

### Existing key with stale Rime session

Validate, remove stale indexes idempotently, rebuild once under `g_api_mutex`, and rebind the same key.

### Client process tracking unavailable

Continue keyed idempotent operation with `owner = nullopt`. Emit a diagnostic at an appropriate level; do not fail normal IPC solely because process cleanup tracking is unavailable.

### Duplicate key from another tracked process

A key already owned by one live `ClientProcessToken` is rejected if presented by a different tracked process token. This catches accidental reuse and makes ownership explicit.

If one side lacks an authoritative token, keyed idempotency remains functional but cross-process ownership checking is necessarily weaker. SessionKey is random and the pipe remains protected by the existing per-user security policy.

## 10. Security Properties

SessionKey is a logical idempotency key, not a bearer authentication credential.

Nevertheless:

- generate it with a CSPRNG;
- never log the full key at normal log levels;
- bind it to the authoritative client-process token when one exists;
- reject tracked cross-process reuse;
- never trust a PID supplied in request text.

The authoritative peer PID is queried from the server-side named-pipe handle. Microsoft documents `GetNamedPipeClientProcessId` for server pipe instances created by `CreateNamedPipe`, supported for desktop applications from Windows Vista onward.

## 11. Test Plan

### 11.1 SessionKey tests

- generated keys are nonzero/well-formed;
- parse/format round trip is exact;
- malformed length/hex is rejected;
- serialization is fixed-width lowercase hex.

Randomness quality itself is delegated to the Windows CSPRNG rather than statistically tested.

### 11.2 SessionManager unit tests

- first `EnsureSession(K, P)` creates exactly one entry;
- repeated ensure with the same `(K, P)` returns the same `ipc_id` and does not call the creation callback again;
- existing mapping with failed validation is destroyed/rebuilt once and reuses K;
- ensure of K from a different tracked process token is rejected;
- removing a session erases both id and key indexes;
- removing the same session twice is harmless;
- `RemoveClient(P)` destroys every session owned by P and no sessions owned by another process;
- `Clear()` leaves every index empty;
- monotonic IPC id allocation remains monotonic across manager clears/maintenance.

### 11.3 START_SESSION integration tests

Using a private test pipe and fake handler/backend:

- send keyed START_SESSION, intentionally lose/discard its response, reconnect, retry with the same key: exactly one backend session was created and the same `ipc_id` is returned;
- retry the same key from a second thread/pipe in the same client process: reuse the same logical session;
- make the backend session stale before retry: retry rebuilds exactly once under the same key;
- old/unkeyed START_SESSION still works;
- malformed key returns 0 and creates nothing;
- same key from a different tracked process token creates nothing.

### 11.4 Process lifetime tests

Abstract process waiting behind a small interface so most tests use a deterministic fake tracker rather than real process timing.

Verify:

- client-process exit removes all token-owned sessions;
- pipe disconnect alone does not remove a session;
- one thread/connection exiting does not remove a session used through another thread;
- PID reuse produces a new process token and cannot inherit old sessions;
- tracker shutdown drains callbacks before `RequestHandler` destruction;
- failure to acquire a process token does not prevent keyed START_SESSION/retry.

One Windows integration test may use a short-lived helper process to validate real `GetNamedPipeClientProcessId` and process-wait plumbing.

### 11.5 EndSession tests

- delivered END_SESSION removes the server entry and local key is retired;
- locally retiring a key followed by a new StartSession uses a different key;
- an old dormant session left by an ambiguous END_SESSION cannot be mistaken for the new logical session;
- process exit removes any such dormant token-owned entry.

### 11.6 Existing regressions

Continue running:

- `TestPipeDeadlinePolicy.ps1`;
- `TestFirstKeyWakeupPolicy.ps1`;
- `TestPipeChannel`;
- `TestServerCommands`;
- full Windows xmake regression suite.

No regression is acceptable in key exactly-once behavior or the one-second first-key recovery budget.

## 12. Implementation Boundaries

Expected production touch points:

- `include/WeaselSessionKey.h` — SessionKey value/parse/format/generation helpers;
- `include/WeaselSessionManager.h` or a server-private equivalent — logical identity/lifetime indexes;
- `WeaselIPC/WeaselClientImpl.{h,cpp}` — create/persist/retire SessionKey and serialize it in START_SESSION;
- `include/PipeServer.h` / `WeaselIPCServer/WeaselServerImpl.*` — authoritative connection process context;
- `include/WeaselIPC.h` — evolve RequestHandler START_SESSION semantics without changing wire framing;
- `RimeWithWeasel/RimeWithWeasel.cpp` / `include/RimeWithWeasel.h` — idempotent create/reuse/rebuild hooks and transactional cleanup;
- focused SessionKey/SessionManager tests and `test/TestServerCommands/*` integration coverage.

The first implementation should not refactor unrelated UI/session-status code merely to make the manager aesthetically complete. Move only identity and lifetime responsibilities required for correctness.

## 13. Migration Sequence

1. Add SessionKey value type and deterministic parse/format tests.
2. Add SessionManager with fake create/validate/destroy callbacks and unit tests.
3. Generate/persist/retire SessionKey in ClientImpl and serialize it in the START_SESSION body.
4. Parse keyed START_SESSION on the server and make creation/retry idempotent.
5. Add stale-Rime validation/rebuild under the same key.
6. Preserve legacy unkeyed path.
7. Add authoritative optional client-process context to PipeServer workers.
8. Add process tracker and token-owned cleanup.
9. Integrate maintenance/finalize transactional index clearing.
10. Add lost-response/reconnect integration regression proving one live logical creation.
11. Run the full existing IPC/TSF regression suite.

Each step must compile and have focused tests before the next lifecycle layer is added.

## 14. Acceptance Criteria

The implementation is acceptable when tests demonstrate all of the following:

- a delivered START_SESSION whose response is lost can be retried without allocating a second live Rime session;
- repeated START retries cannot grow the session table for one SessionKey;
- a stale underlying Rime session is rebuilt once under the same SessionKey rather than returned as a dead handle;
- logical sessions survive individual pipe/thread reconnects;
- client-process exit deterministically removes sessions for which process tracking is available;
- failure of process tracking does not break keyed idempotency;
- old unkeyed clients continue to establish sessions against the new server;
- new keyed clients still operate against an old server, although old-server retries remain non-idempotent during the mixed-version window;
- maintenance/finalize leave no stale SessionKey or ownership indexes;
- a retired SessionKey is never reused for a later logical session;
- healthy PROCESS_KEY performs no SessionKey lookup or process-liveness check;
- existing key exactly-once and first-key recovery regression tests remain green.
