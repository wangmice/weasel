# K22⑤ 回归分析：Telegram(AyuGram) 每键双字符（2026-09-15）

## 结论速览

- 坏 commit：`3514b28` fix(WeaselTSF): reset the key-event pending state per key identity — K22⑤（bisect 实测定位，首个 bad commit）。
- 症状：AyuGram（Telegram 桌面版分支，Qt 程序）切中文后每按一键上屏两个相同字符；浏览器、VSCode 等正常。
- 根因：K22⑤ 把 OnTestKeyDown/OnKeyDown 的去重判据从布尔标志改成了 (wParam, lParam) 精确相等。TSF 对配对回调的参数一致性没有任何契约——TSF-aware 应用（浏览器/VSCode 自己调 keystroke manager，参数取自消息结构，Test 与 Key 字节一致）恰好满足；而非 TSF-aware 应用（Qt 系如 Telegram）的键事件由系统 CUAS/IMM32 兼容桥代投递，Test 与 Key 两次调用的 lParam 各位（repeat count、previous key state、扫描码重建等）可以不同。于是 Telegram 里每次按键的 Key 回调都通不过判重，被当成新键再次送进 rime 服务器：Test 一次、Key 一次，composition 里落下两个字符。
- 修复：`204fb4a`（master）。去重身份放宽为虚键码（wParam）：虚键码恰是 weasel 消费的全部键身份（`KeyEvent.cpp` 中 scanCode/extended 只在 vkey 内细分左右 Shift、小键盘 Enter 等，repeat/prev-state 位完全不参与）；不同 vkey 到来即开新周期，保留 K22⑤ 对"应用吞掉配对回调"场景的修复。另在 `OnSetFocus` 复位两侧 pending（焦点切换后配对必然失效）。
- 教训一句话：给外部回调的原始参数加"精确相等"判据，等于替调用方发明了契约里不存在的保证；匹配边界应当是组件自己消费的语义身份，而不是参数字节。

## 背景机制

### 键到输入法的两条投递路径

**TSF-aware 应用**（Chromium 系：Chrome/Edge、VSCode/Electron、Office 等）：应用自己在消息循环里持有 `ITfKeystrokeMgr`，按键到达时依次调 `TestKeyDown(wParam, lParam)` 探测、视结果调 `KeyDown(wParam, lParam)`。两个参数都直接来自 `WM_KEYDOWN` 的消息结构，配对调用字节一致。

**非 TSF-aware 应用**（Qt 系：Telegram/AyuGram、QQ2012 一类的老程序）：应用不认识 TSF，按键照常走 `TranslateMessage` 等传统 API；系统在其进程中加载 CUAS（Cicero Unaware Application Support，IMM32 兼容桥），由它在内部把键转发给 `ITfKeystrokeMgr::TestKeyDown/KeyDown`。**这两次调用的参数由系统桥自行构造**，从属于桥的实现细节，没有任何文档承诺 Test 与 Key 携带相同的 lParam。lParam 中的 repeat count（bit 0-15）、previous key state（bit 30）本就是随时间/上下文漂移的位，扫描码也可能是重建值。

佐证：tdesktop（Telegram 桌面版上游）源码中 gh 代码搜索无任何 `ITfKeystrokeMgr`/`ImmProcessKey` 引用；Qt 5.15 的 `qwindowsinputcontext.cpp` 只处理 composition 生命周期消息（`WM_IME_*`），不做按键转发——按键到 IME 的投递完全发生在系统层，即 CUAS 桥。MS 文档 `ITfKeyEventSink::OnTestKeyDown` 只描述参数含义，对配对一致性只字未提。

### weasel 去重状态机的三代演变

weasel 的 `OnTestKeyDown` 不是纯探测——它直接把键送进 rime 服务器并更新 composition，靠"pending 标志 + Key 回调判重"防止同一次按键送两次。历史序列：

1. **上游原版（布尔标志）**：TestKeyDown eaten 后置 `pending=TRUE`；任何 KeyDown 到来都视为配对、直接 eaten 不再送服务器。缺陷（K22⑤ 要修的）：若应用在 eaten 的 Test 之后吞掉按键、不再回调 OnKeyDown，布尔 pending 永久挂起，下一个键的 Test 被判重"不送服务器"直接 eaten——丢一个字符。
2. **K22⑤（`3514b28`，精确身份）**：pending 记录 (wParam, lParam)，判重要求完全相等，不同键即开新周期。上表缺陷修掉了，但引入本次回归：CUAS 应用的配对 lParam 不同 → 判重失败 → OnKeyDown 再送一次服务器 → 每键两字符。
3. **本次修复（`204fb4a`，虚键码身份）**：pending 只记 vkey。见下节论证。

### 为什么虚键码是正确的匹配边界

`ConvertKeyEvent`（`WeaselTSF/KeyEvent.cpp`）消费的键身份是：`vkey` 查 `TranslateKeycode` 得 ibus keycode；`scanCode` 仅用于区分 VK_SHIFT 的左右；`isExtended` 仅用于区分 VK_RETURN/VK_CONTROL 的左右。repeat count 与 previous-state 位**完全不参与**键的语义。因此：

- "同一次按键的 Test 与 Key 回调"对 weasel 的全部可观察差异只有 vkey——用 vkey 判重，不多不少；
- lParam 精确相等是一个比组件自身语义强得多的隐含假设，一旦调用方（CUAS 桥）不满足，配对即断裂；
- 反方向（布尔）又太松：不同键也无法区分，才有"残留 pending 吞掉下一个键"的原缺陷。

虚键码恰好是两者唯一的交叠信息：同一物理按键的配对回调 vkey 恒同（0xE5/VK_PROCESSKEY 的改写发生在 IME 处理之后的应用消息层，不影响送进 keystroke manager 的值），不同键 vkey 恒异。

## 修复内容（`204fb4a`）

- `WeaselTSF.h`：`PendingTestKey` 由 `{pending, wParam, lParam}` 改为 `{pending, vkey}`，注释说明身份选取依据。
- `WeaselTSF/KeyEventSink.cpp`：四个回调（OnTestKeyDown/OnKeyDown/OnTestKeyUp/OnKeyUp）的判重条件由三项精确相等改为 `vkey == wParam`；置位处只记 vkey。
- `OnSetFocus`：进入时复位两侧 pending——焦点切换后任何等待配对的 pending 按定义失效，把"吞配对"类残留的影响域进一步压缩到单个焦点会话内。
- 头部块注释改写，记录 CUAS 桥参数不一致这一事实，防止后人再犯。

## 场景推演（三代对照）

| 调用序列 | 布尔（上游/K22⑤ 前） | K22⑤（精确匹配） | VK 身份（本次） |
|---|---|---|---|
| TSF-aware：Test(a,lp) → Key(a,lp) | 送 1 次 | 送 1 次 | 送 1 次 |
| CUAS/Qt（Telegram）：Test(a,lp1) → Key(a,lp2) | 送 1 次 | 送 2 次（回归） | 送 1 次 |
| WORD 2010：Test(a,lp) × N → Key(a,lp) | 送 1 次 | 送 1 次 | 送 1 次 |
| 吞配对：Test(a) eaten，无 Key → Test(b) | b 丢失（原缺陷） | b 正常 | b 正常 |
| 吞配对后同键重按：… → Test(a) | a 丢失 | a 丢失 | a 丢失（残余限制，见下） |
| QQ2012：仅 Key(a) | 正常 | 正常 | 正常 |
| 自动重复（正常应用逐 repeat 配对） | 每 repeat 1 次 | 每 repeat 1 次 | 每 repeat 1 次 |

## 残余限制

应用吞掉配对回调、且连 key-up 也不回调时，同一 vkey 的下一次按下仍会被漏送一次，直到任何 key 事件交叉清除或焦点切换（新增复位）到达。这是键身份信息量所限：无身份判不出"新按键的 Test"与"同键的重复 Test"。该窗口远窄于修复前的"任意后续键被吞"，且此类应用本身（吞 Test 之后的配对键）目前只是推演出的假想调用方，无实测 repro。

若实测发现 Telegram 仍双字符（即 CUAS 桥的 Key 回调连 wParam 都不同），下一步的判据只能退回"顺序配对"（eaten 的 Test 之后下一个 Key 无条件视为配对、以 Test 侧的 vkey 判新键），并先用日志插桩记录 (event, wParam, lParam) 四元组序列确认。

## 教训

1. **精确相等匹配是替调用方发明契约**。TSF 只承诺"把 wParam/lParam 传给你"，从不承诺配对调用参数一致。对外部回调做去重/配对时，匹配边界应取组件自身消费的语义身份（此处为 vkey），而不是原始参数字节；比语义身份更严格的条件总有一天被某个调用方打破。
2. **推演验证的覆盖率取决于调用方类别枚举，不是序列数量**。批次 14 的 K22⑤ 记录声称"全量状态机推演含上述 5 类序列"，五类序列全绿——但序列全部枚举自同一类调用方视角，漏掉了"调用方是系统 CUAS 桥而非应用本身"这整个类别。状态机推演前先枚举 caller class。
3. **防御性修复没有真实 repro 时，风险折向看不见的用户**。K22⑤ 修的"吞配对"场景是推演假想，引入的回归却击中真实的 Telegram/Qt 用户群。无 repro 的防御性改动，边界条件应取最宽松仍成立的判据（本例：vkey 而非精确 lParam），把"未知调用方行为"的假设面压到最小。
4. **上游逐字一致不构成安全背书**。上游布尔版同样有丢键缺陷，只是"偶丢一个字符"远比"每键双字符"隐蔽。bisect 是把推演打回原形的最终手段——本次若无 bisect，回归会随安装包分发。

## 验证与证据

- 修复后全量 release 构建 ok（`xmake`，weaselx64.dll 链接通过）。`TestKeyEvent` 与本次改动无编译单元交集（其只编译 `KeyEvent.cpp`/`KeyEvent.h`，二者未动），18 项断言不受影响。
- 待实测：AyuGram 中文打字每键单字符（构建产物 `build\windows\x64\release\WeaselTSF\weaselx64.dll`，安装后验证）；浏览器/VSCode 行为应与之前一致。
- 证据链：
  - bisect：`3514b28` 为首个 bad commit（用户实测）；
  - tdesktop 源码（gh 代码搜索）无 TSF/ImmProcessKey 引用；Qt 5.15 `qwindowsinputcontext.cpp` 无按键转发——Telegram 按键投递走系统 CUAS 桥；
  - MS 文档 `ITfKeyEventSink::OnTestKeyDown/OnKeyDown` 无配对参数一致性契约；
  - `ConvertKeyEvent`/`TranslateKeycode`（KeyEvent.cpp）消费的键身份 = vkey +（vkey 内细分的）scanCode/extended；
  - 症状侧：每键恰两个相同字符 = Test 与 Key 各送服务器一次；浏览器/VSCode（参数一致的直调方）不受影响，与"仅参数不一致的调用方受影响"的推论吻合。
