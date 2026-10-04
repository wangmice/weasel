# Quick Word Helper Design

## Baseline

- Weasel: `31944f963587a2ebc8ebb69a2f0859d2c0cfd8ef`
- librime submodule: `d4b388a06ddf4e7091e8afddbbc4a442fabbed9b`

## Goal

Replace the in-process `WeaselServer` quick-word modal dialog with a dedicated `WeaselQuickWord.exe` helper so that the dialog reliably appears in front of the active application and can use Weasel itself for Chinese input. Add schema-aware automatic code generation so quick-word codes always follow the active Rime input schema (for example, Pinyin and Wubi remain separate).

## User experience

1. User presses the configurable quick-word shortcut while a Weasel session is active.
2. Weasel opens or foregrounds a single `WeaselQuickWord.exe` instance associated with the originating session.
3. The helper shows a normal editable word field and code field. The helper is a regular Windows process, so TSF input works normally inside its edit controls.
4. After the word text changes, the helper requests code suggestions from `WeaselServer` with a short debounce.
5. `WeaselServer` asks librime to generate codes from the originating session's active schema.
6. The best code is filled automatically. Up to 8 alternatives may be offered for ambiguous readings/codes. Manual edits are never overwritten until the word text changes again.
7. Save sends the chosen text/code back to `WeaselServer`, which writes through the librime user-dictionary API and refreshes the active Weasel UI.

## Architecture

### WeaselTSF

Keep the existing preserved-key registration and `WEASEL_IPC_OPEN_QUICK_WORD` request. The TSF layer must not create UI or manipulate librime directly.

### WeaselServer

Remove the modal `QuickWordDialog` from `WeaselServerApp.cpp`. The server becomes the coordinator:

- validate the originating session;
- capture the foreground window information needed for initial helper placement;
- launch `WeaselQuickWord.exe` if it is not running;
- foreground the existing helper if it is already running;
- service helper requests for code suggestions and final insertion;
- serialize librime calls through the existing server/API synchronization path.

The server must not block its UI/message thread waiting for the helper to close.

### WeaselQuickWord.exe

Add a small Win32/WTL GUI executable dedicated to quick-word UI. It owns no librime state and does not link directly to `rime.dll`.

Responsibilities:

- single-instance window per logged-in user;
- normal top-level dialog/window with ordinary edit controls;
- initial foreground/owner handling and centering near the originating foreground window;
- word editing, code suggestion display, manual override tracking, validation and user-facing errors;
- IPC to `WeaselServer` only.

The helper must not use permanent `WS_EX_TOPMOST`. It may use the temporary TOPMOST -> foreground -> NOTOPMOST sequence only for initial activation when Windows foreground rules allow it.

### IPC

Keep `WEASEL_IPC_OPEN_QUICK_WORD` for TSF -> server. Add helper-facing requests with explicit payloads rather than overloading `PipeMessage` integer fields:

- `QUERY_QUICK_WORD_CODES(session_id, text)`
- `ADD_QUICK_WORD(session_id, text, code)`

Responses:

- code-query result: success flag, active schema id/name where available, and up to 8 UTF-8/UTF-16 code strings;
- add result: success/failure plus a small stable error code (`invalid_session`, `unsupported_schema`, `no_code`, `write_failed`).

Payload parsing must be bounded by existing IPC buffer limits and reject malformed/oversized requests.

## librime API

Extend `RimeUserDictionaryApi` while preserving `add_user_entry` compatibility.

Add a schema/session-aware suggestion function. The public API should expose caller-owned buffers rather than C++ containers, for ABI stability. Conceptually:

```c
Bool suggest_entry_codes(RimeSessionId session_id,
                         const char* text,
                         RimeUserDictionaryCodeList* result);
```

The implementation resolves the active schema from `session_id` and only uses writable table/script translator configuration belonging to that schema.

### Script translator behavior

For Pinyin-like script schemas:

1. prefer full-word dictionary readings when available;
2. otherwise derive candidate syllable sequences from dictionary/prism data;
3. retain multiple valid readings, capped at 8;
4. normalize every returned code through the active prism/spelling rules.

### Table translator behavior

For Wubi/Cangjie/Zhengma-like table schemas:

1. use the active table translator dictionary/encoder configuration;
2. use the schema's phrase-encoding rules when the encoder can derive the phrase code;
3. never fall back to Pinyin;
4. return no automatic suggestion when the active schema cannot derive a code, leaving manual entry available.

## Repository correction required before feature implementation

The current pushed librime baseline contains `src/rime_user_dictionary_api.h`, but the repository tree at `d4b388a` does not contain the previously built `plugins/user_dictionary_api/...` implementation. The feature work must first restore/commit the actual implementation and tests into the librime fork, then extend that implementation with code suggestion support. Weasel must update its librime submodule only after the librime commit is complete.

## Build and packaging

- Add `WeaselQuickWord` to `weasel.sln` for Win32 and x64.
- Add project resources and localized strings for the helper.
- Package `WeaselQuickWord.exe` in `output/install.nsi` for the corresponding architectures.
- Keep ARM/ARM64 behavior unchanged unless those configurations already build the new project automatically.
- The helper links to Weasel IPC/common utility libraries only, not librime.

## Failure handling

- Invalid/stale originating session: show a clear retry message and do not write through another session.
- No generated code: leave code editable and explain that the active schema requires manual code.
- Helper already running: reuse and foreground it instead of opening nested dialogs.
- Server unavailable: helper reports service unavailable and can retry; it must not manipulate userdb directly.
- librime API unavailable/version mismatch: fail gracefully and preserve manual text without crashing the server.

## Tests

### librime

Add focused tests for:

- schema selection by session;
- script schema automatic code generation;
- ambiguous code candidate cap/deduplication;
- table schema phrase encoder behavior;
- unsupported/no-code schema behavior;
- existing `add_user_entry` regression coverage.

### Weasel

Add unit-testable IPC payload encode/decode tests and helper state tests for:

- manual-code override is preserved until text changes;
- stale session response handling;
- single-instance activation request handling;
- malformed/oversized payload rejection.

Manual Windows verification:

- dialog appears above the invoking app without permanent topmost;
- Weasel Chinese input works inside the helper text field;
- Pinyin schema produces Pinyin codes;
- Wubi schema produces only Wubi/table-derived codes;
- switching schema before reopening quick-word changes the generated code source;
- saved entry remains after WeaselServer restart.

## Non-goals

- No deployment/rebuild after inserting a quick word.
- No direct userdb access from the helper.
- No hard-coded Pinyin conversion in Weasel.
- No reuse of `WeaselDeployer.exe` for quick-word UI.
- No permanent global-topmost behavior.
