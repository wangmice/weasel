# Quick Word Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an independent `WeaselQuickWord.exe` that can accept Chinese input, auto-generate codes using the active Rime schema, and save through the existing online user-dictionary path.

**Architecture:** Extend the librime custom user-dictionary module with schema-aware code suggestion, expose bounded quick-word request/response methods through Weasel IPC, then move UI out of `WeaselServer` into a dedicated helper process. The server remains the sole owner of librime/session state and the helper stays a thin UI/IPC client.

**Tech Stack:** C++17, librime custom C API, Boost/WTL/Win32, Weasel named-pipe IPC, xmake, MSBuild/Visual Studio 2022, NSIS, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-04-quick-word-helper-design.md`

## Global Constraints

- Baseline Weasel commit: `98efbdde084ce0cb0020f0c3ad617863b71c79f1`.
- Baseline librime commit: `6b92c9a54d4a968977c5b1c50c45d960fe6aae3d`.
- Preserve `RimeUserDictionaryApi::add_user_entry` compatibility.
- Automatic codes always come from the originating session's active schema; never cross-fallback between Pinyin/table schemas.
- Return at most 8 automatic codes and deduplicate them.
- Helper never links to librime or writes userdb directly.
- Helper is not permanently topmost and must not block the WeaselServer UI/message thread.
- Existing Win32/x64 builds, xmake, MSBuild, and installer flows remain supported.

## Review Focus

- Multi-byte UTF-8 text near the API/IPC limits must reject cleanly without truncating a code point or overflowing fixed buffers.
- A session destroyed after opening quick-word but before query/save must fail rather than silently switching to another active session.
- Schemas containing multiple table/script translators must pick a compatible writable translator deterministically and keep suggestion/save behavior aligned.
- Table schemas without deployed encoder metadata must return no auto code while preserving manual save support.
- Repeated shortcut presses while the helper exists must foreground/re-target the single instance without spawning nested windows or losing manually entered text unexpectedly.

---

### Task 1: librime schema-aware code suggestion API

**Files:**
- Modify: `src/rime_user_dictionary_api.h`
- Modify: `plugins/user_dictionary_api/user_dictionary_api.cc`
- Create: `plugins/user_dictionary_api/user_dictionary_api_test.cc`
- Modify: `plugins/user_dictionary_api/CMakeLists.txt`

**Interfaces:**
- Consumes: active `RimeSessionId`, translator namespace, deployed reverse dictionary, `ScriptEncoder`/`TableEncoder`.
- Produces: `Bool (*suggest_entry_codes)(RimeSessionId, const char*, int, RimeUserDictionaryCodeCallback, void*)`; existing `add_user_entry` remains unchanged.

- [ ] **Step 1: Write failing tests** for script candidates, table encoder candidates, dedup/cap=8, invalid session, no encoder/no code, and existing add API compatibility.
- [ ] **Step 2: Run the focused librime plugin test and verify RED.** Expected: compile/link failure because `suggest_entry_codes` is absent.
- [ ] **Step 3: Implement callback ABI and a bounded collector** that reverse-lookups source word codes and feeds `ScriptEncoder` or `TableEncoder`; table encoder loads settings from the active reverse dictionary metadata.
- [ ] **Step 4: Run focused tests and full librime tests.** Expected: PASS with no warnings/errors.
- [ ] **Step 5: Commit** `feat(api): suggest user dictionary entry codes`.

### Task 2: Weasel quick-word IPC contract

**Files:**
- Modify: `include/WeaselIPC.h`
- Modify: `WeaselIPC/WeaselClientImpl.h`
- Modify: `WeaselIPC/WeaselClientImpl.cpp`
- Modify: `WeaselIPCServer/WeaselServerImpl.h`
- Modify: `WeaselIPCServer/WeaselServerImpl.cpp`
- Modify: `RimeWithWeasel/RimeWithWeasel.h`
- Modify: `RimeWithWeasel/RimeWithWeasel.cpp`
- Create: `include/QuickWordIPC.h`
- Create: `test/TestQuickWordIPC/TestQuickWordIPC.cpp`
- Create: `test/TestQuickWordIPC/xmake.lua`
- Add corresponding MSBuild test project files if the solution's debug test suite requires them.

**Interfaces:**
- Consumes: Task 1 `suggest_entry_codes` and existing `add_user_entry`.
- Produces: `Client::QueryQuickWordCodes(session_id,text,...)` and `Client::AddQuickWord(session_id,text,code)` helper-facing calls; server request-handler methods return bounded structured results.

- [ ] **Step 1: Write failing parser/serialization tests** for UTF-16 payloads, malformed bodies, maximum lengths, >8 responses, stale session, and error classification.
- [ ] **Step 2: Run TestQuickWordIPC and verify RED.** Expected: missing QuickWordIPC API/types.
- [ ] **Step 3: Implement bounded line/body codec and IPC commands** using the existing named-pipe body buffer; do not overload integer parameters for text.
- [ ] **Step 4: Wire RimeWithWeasel suggestion/save methods** to the custom librime API under existing server serialization.
- [ ] **Step 5: Run TestQuickWordIPC plus existing debug tests.** Expected: PASS.
- [ ] **Step 6: Commit** `feat(quick-word): add schema-aware IPC operations`.

### Task 3: independent WeaselQuickWord helper

**Files:**
- Create: `WeaselQuickWord/QuickWordModel.h`
- Create: `WeaselQuickWord/QuickWordModel.cpp`
- Create: `WeaselQuickWord/WeaselQuickWord.cpp`
- Create: `WeaselQuickWord/QuickWordDialog.h`
- Create: `WeaselQuickWord/QuickWordDialog.cpp`
- Create: `WeaselQuickWord/resource.h`
- Create: `WeaselQuickWord/WeaselQuickWord.rc`
- Create: `WeaselQuickWord/xmake.lua`
- Create/modify VS project files and `weasel.sln`.
- Create: `test/TestQuickWordModel/TestQuickWordModel.cpp`
- Create: `test/TestQuickWordModel/xmake.lua`

**Interfaces:**
- Consumes: Task 2 helper-facing IPC calls.
- Produces: single-instance `WeaselQuickWord.exe` with `--session <id>` activation and UI model preserving manual-code override until word text changes.

- [ ] **Step 1: Write failing model tests** for auto-fill, manual override, text-change reset, stale-session error preservation, and activation retarget behavior.
- [ ] **Step 2: Run TestQuickWordModel and verify RED.** Expected: model API missing.
- [ ] **Step 3: Implement model then Win32/WTL dialog** with 250ms debounce, editable code combo/list, initial foreground promotion without permanent TOPMOST, and retry-safe errors.
- [ ] **Step 4: Implement per-user single-instance activation** so a second launch forwards the new session to the existing helper and exits.
- [ ] **Step 5: Run model tests and build helper for Win32/x64.** Expected: PASS/build clean.
- [ ] **Step 6: Commit** `feat(quick-word): add independent helper UI`.

### Task 4: server launch integration and remove in-process modal UI

**Files:**
- Modify: `WeaselServer/WeaselServerApp.cpp`
- Modify: `WeaselServer/WeaselServerApp.h`
- Modify: `WeaselIPCServer/WeaselServerImpl.cpp`
- Modify: `WeaselIPCServer/WeaselServerImpl.h`

**Interfaces:**
- Consumes: Task 3 helper executable/activation contract.
- Produces: existing `WEASEL_IPC_OPEN_QUICK_WORD` asynchronously launches/activates helper for the originating valid session.

- [ ] **Step 1: Add a failing testable launch-command helper** proving session id is preserved and invalid sessions do not launch.
- [ ] **Step 2: Verify RED.** Expected: launch-command helper absent.
- [ ] **Step 3: Remove `QuickWordDialog` from WeaselServer** and replace callback with asynchronous helper launch/activation; capture foreground HWND metadata only for initial placement if usable.
- [ ] **Step 4: Run existing IPC/server tests and build WeaselServer.** Expected: PASS/build clean.
- [ ] **Step 5: Commit** `refactor(quick-word): move dialog out of server`.

### Task 5: build, installer, CI and final verification

**Files:**
- Modify: root `xmake.lua`
- Modify: `weasel.sln`
- Modify/create: `WeaselQuickWord/*.vcxproj*`
- Modify: `output/install.nsi`
- Modify: build scripts only where the existing project discovery does not include the new target automatically.

**Interfaces:**
- Consumes: Tasks 1-4 complete artifacts.
- Produces: Win32/x64 installer containing `WeaselQuickWord.exe`; both xmake and MSBuild build variants succeed.

- [ ] **Step 1: Add packaging/build assertions** (installer file inclusion and target presence) before adding the production packaging lines.
- [ ] **Step 2: Verify RED** by running the relevant build/package check.
- [ ] **Step 3: Add helper target to xmake/solution/project and installer.**
- [ ] **Step 4: Run full Weasel CI-equivalent builds** for xmake and MSBuild plus debug tests; run librime full tests.
- [ ] **Step 5: Manual Windows verification checklist**: foreground behavior, Chinese typing inside helper, Pinyin code, Wubi/table code, schema switch, stale session, persistence after server restart.
- [ ] **Step 6: Commit** `build(quick-word): package helper executable`.
