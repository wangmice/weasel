# Quick Word Helper Design

## Baseline

- Weasel: `98efbdde084ce0cb0020f0c3ad617863b71c79f1`
- librime submodule: `6b92c9a54d4a968977c5b1c50c45d960fe6aae3d`

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
- initial foreground handling and centering near the originating foreground window;
- word editing, code suggestion display, manual override tracking, validation and user-facing errors;
- IPC to `WeaselServer` only.

The helper must not use permanent `WS_EX_TOPMOST`. It may use the temporary TOPMOST -> foreground -> NOTOPMOST sequence only for initial activation when Windows foreground rules allow it.

### IPC

Keep `WEASEL_IPC_OPEN_QUICK_WORD` for TSF -> server. Add helper-facing requests with bounded text payloads:

- `QUERY_QUICK_WORD_CODES(session_id, text)`
- `ADD_QUICK_WORD(session_id, text, code)`

Responses:

- code query: success flag, active schema metadata where useful, and up to 8 codes;
- add result: success/failure with a stable error classification where feasible.

Payload parsing must remain bounded by the existing IPC buffer and reject malformed/oversized requests.

## librime API

Extend `RimeUserDictionaryApi` while preserving `add_user_entry` ABI compatibility. Add a session/schema-aware suggestion callback API; librime owns no caller buffers and emits at most 8 codes.

### Script translator behavior

Use the active translator dictionary reverse lookup plus `ScriptEncoder` so full-word readings are preferred when available and the encoder can fall back to shorter words/characters. Deduplicate candidates and cap at 8.

### Table translator behavior

Use the active translator reverse lookup plus `TableEncoder` loaded from that dictionary's deployed encoder settings. Never fall back to Pinyin. If the active table schema has no usable encoder rules or source codes, return no automatic suggestion and leave manual entry available.

## Build and packaging

- Add `WeaselQuickWord` to xmake and `weasel.sln` for Win32/x64.
- Package `WeaselQuickWord.exe` in the installer.
- The helper links to Weasel IPC/common libraries only, not librime.

## Failure handling

- Invalid/stale originating session: fail explicitly and do not write through another session.
- No generated code: leave code editable and explain that the active schema requires manual code.
- Helper already running: reuse and foreground it instead of opening nested dialogs.
- Server unavailable: preserve entered text and allow retry.
- librime API unavailable/version mismatch: fail gracefully without crashing the server.

## Tests

### librime

Cover script generation, ambiguous candidates/cap/deduplication, table encoder behavior, unsupported/no-code behavior, and existing add-user-entry regression.

### Weasel

Cover IPC payload parsing/limits and helper state rules (manual override until text changes, stale session, single-instance activation request state).

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
