# Weasel（小狼毫）代码审查报告

- **日期**：2026-09-06
- **审查范围**：`WeaselTSF`、`WeaselServer`、`WeaselIPCServer`、`WeaselIPC`、`RimeWithWeasel`、`WeaselUI`、`WeaselDeployer`、`WeaselSetup`、`update/` 及 `include/` 共享头文件（约 6.3 万行 C++），并复查了 master 上最近 15 个 commit 的改动。
- **方法**：3 路并行深度代码审查 + 逐条人工复核原始代码 + 本地可验证项独立复现实验。
- **构建验证**：`xmake -y` 全量构建成功（release，39.3s，全部 target 编译链接通过）。

> **验证状态标记**
> - ✅ 已本地复现：用独立程序在真实环境复现了该逻辑缺陷
> - ✔ 代码核实：已逐行读原始代码确认缺陷存在（未做运行时复现）
> - ⚠ 子代理报告：来自深度审查代理，结论可信但未逐行复核
> - ❌ 被推翻：审查中发现后经核实为误报

---

## 0. 本地验证结果总览

审查共报告 **40+ 项问题**，对其中可独立验证的 6 项做了本地实验：

| # | 被验证的问题 | 位置 | 实验结果 |
|---|---|---|---|
| V1 | 高亮区间 `substr(pos, end)` 误用（第二参是 count 不是 end） | `WeaselUI/StandardLayout.cpp:98` | ✅ 复现：`substr(3,5)` 得到 `"defgh"`（长 5），正确应为 `"de"`（长 2） |
| V2 | `vec[2]` 越界读（只检查了 `size() < 2`） | `WeaselIPC/ContextUpdater.cpp:55-62` | ✅ 复现：MSVC 检查迭代器下触发断言 `vector subscript out of range` |
| V3 | `SysAllocStringLen(len+1)` BSTR 长度差一 | `WeaselTSF/CandidateList.cpp:127` | ✅ 复现：源串 3 字符，`SysStringLen` 返回 4（内含多余 NUL） |
| V4 | `GetLastError()` 在 `CreateMutex` 后被管道调用覆盖 | `WeaselTSF/WeaselTSF.cpp:261` | ✅ 复现：`ERROR_ALREADY_EXISTS(183)` 在一次失败管道调用后变为 `ERROR_FILE_NOT_FOUND(2)` |
| V5 | `bump-version.ps1` 用字符串比较版本号 | `update/bump-version.ps1:177` | ✅ 复现：PowerShell 中 `'0.10.0' -lt '0.9.0'` 为 `True`（`[version]` 比较为 `False`） |
| V6 | UIStyle::AntiAliasMode 与 D2D 枚举值错位（子代理报告） | `include/WeaselIPCData.h:196-202` | ❌ **被推翻**：本机 SDK `d2d1.h` 中 `CLEARTYPE=1, GRAYSCALE=2, ALIASED=3`，与 `WeaselIPCData.h` 完全一致，无错位 |

辅助佐证（只读检查，未干扰正在运行的输入法）：
- 本机 `WeaselServer.exe` 正在运行（PID 20736），TSF 已注册（`HKLM\SOFTWARE\Microsoft\CTF\TIP\{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}` 存在）。
- `HKCU\Software\Rime\Weasel` 存在 `RimeUserDir`、`Hant=0` 但**无** `Profile` 值——这正是 S3（卸载时 profile 回退可能删错 profile）所依赖的真实状态。
- 复现程序位于 `%TEMP%\weasel_review\repro.cpp`（不污染仓库），运行输出见附录 A。

---

## 1. 高危问题（P1）

### IPC / 服务端

**1.1 服务端把 `ERROR_PIPE_CONNECTED` 当致命错误，掐断已建立的连接** ✔ 代码核实
- `WeaselIPC/PipeChannel.cpp:104-113`（`_ConnectServerPipe`），调用方 `WeaselIPCServer/WeaselServerImpl.cpp:421-434`（`Listen`）
- 客户端若在服务端 `CreateNamedPipe` 与 `ConnectNamedPipe` 之间完成 `CreateFile`，`ConnectNamedPipe` 会以 `ERROR_PIPE_CONNECTED(535)` 失败——但此时管道**已连接且有效**。当前代码直接 `_ThrowLastError` 抛出，`Listen` 的 `catch (DWORD)` 执行 `_FinalizePipe` 把刚连上的客户端 Disconnect + CloseHandle。在服务端重启/部署/多应用同时打字的场景下造成无谓的断连重连风暴，丢击键、加延迟。
- 修复：`if (!::ConnectNamedPipe(pipe, NULL) && ::GetLastError() != ERROR_PIPE_CONNECTED) _ThrowLastError;`

**1.2 `FlushFileBuffers` 在全局 `g_api_mutex` 内调用：一个挂起的客户端可冻结全局打字** ✔ 代码核实
- `WeaselIPC/PipeChannel.cpp:71-78`（`_WritePipe`）+ `WeaselIPCServer/WeaselServerImpl.cpp:187-190`
- 消息模式下 `FlushFileBuffers` 会阻塞到**对端读完**数据为止。服务端的所有管道处理（包括响应写回）都在持有 `g_api_mutex` 期间进行。若某客户端发出击键请求后在读响应前挂起（调试断点、程序假死），服务端工作线程将**无限期阻塞在 `FlushFileBuffers` 且持有全局锁**——其它所有应用的击键全部排队等待，直到该客户端恢复或退出。这是击键路径上最严重的可用性风险。
- 修复：删除两侧的 `FlushFileBuffers`（消息模式 `WriteFile` 原子投递 + 客户端阻塞 `ReadFile` 已保证同步，flush 完全冗余）；至少也应把 `resp()` 的调用移出锁外。顺带消除 `_Send` 失败重试路径中"请求可能已被服务端读取却又重发"的重复击键隐患（`include/PipeChannel.h:168-173`）。

**1.3 服务端读请求 body 偏移 +12 字节：`START_SESSION` 的首行被悄悄截掉 `"action"`** ✔ 代码核实（含"为何至今没炸"链路）
- `include/PipeChannel.h:137`（`ReceiveBuffer()` 返回 `buffer + _ResSize`）vs `WeaselIPC/PipeChannel.cpp:88-102`（`ERROR_MORE_DATA` 时余量落在 `buffer[0]`）vs `WeaselIPCServer/WeaselServerImpl.cpp:212-217`（`AddSession(channel->ReceiveBuffer(), ...)`）
- 客户端发送 `[12B 头][body]`，服务端读头后剩余 body 实际落在 `buffer[0]`，而 `ReceiveBuffer()` 指向 `buffer+12`——恰好是 body 前 6 个宽字符 `"action"`。因此 `_ReadClientInfo` 收到的是 `"=session\n…"`。目前不炸纯属侥幸：`RimeWithWeasel/RimeWithWeasel.cpp:408-427` 只搜索 `session.client_app=`，从不解析第一行。任何人将来在第一行加字段或解析 `action=` 都会无声坏掉。自 2018 年重构（7418d2b）起即如此。
- 修复：让余量统一落在 `buffer[0]`（服务端对称于客户端的 `HandleResponseData`），或提供明确的 `BodyBuffer()`。

**1.4 主线程路径无锁访问 handler 状态：暗色模式切换可崩服务** ✔ 代码核实
- `WeaselIPCServer/WeaselServerImpl.cpp:56-65`（`OnColorChange`）
- 所有管道工作线程的调用由 `g_api_mutex` 串行化，但 `OnColorChange`（WM_SETTINGCHANGE）在**主线程**直接调 `m_pRequestHandler->UpdateColorTheme()`——后者遍历 `m_session_status_map` 并调 librime。与工作线程的 `AddSession` 插入 map 并发即 UB → 崩溃。托盘菜单的 `SetOption` 同理。
- 修复：主线程对 handler 的调用纳入同一串行化（投递到管道线程或加锁——注意托盘不能无限期阻塞，投递更稳）。

**1.5 退出顺序缺陷：管道监听线程不停，先销毁 handler/UI → use-after-free 窗口** ✔ 代码核实
- `WeaselServerImpl.cpp:42-54`（`_Finailize`：`pipeThread = nullptr` 对 `boost::thread` 是 **detach 不是 join**）、`:421-434`（`Listen` 的 lambda 按引用捕获栈上 `handler`）、`WeaselServerApp` 的析构顺序
- `WM_QUIT` 后 `Run()` 返回，`Finalize()`/成员析构进行时管道线程仍在跑：迟到的请求会调用已销毁的栈上 `std::function`（UAF），工作线程中的 `ProcessKeyEvent` 可与 `rime_api->finalize()` 竞争；随后 `RegisterApplicationRestart` 又把进程拉起，掩盖崩溃。
- 修复：显式有序关闭——interrupt **并 join** 管道线程，先让监听 `ConnectNamedPipe` 解除阻塞，排空在途请求后再 Finalize。

**1.6 每次击键一次 `Echo()` 往返：白白翻倍击键 IPC 延迟** ✔ 代码核实
- `WeaselTSF/WeaselTSF.cpp:239-240`（`_EnsureServerConnected`）由 `KeyEventSink.cpp:20` 每键调用
- 每个键（包括 ASCII 直通键）先做一次 `Echo` 管道往返再做 `ProcessKeyEvent` 往返；服务端不可用时更叠加 reconnect + 第三次 Echo。管道断裂本来就能在 transact 中同步感知。
- 修复：信任已建立的会话，仅在 transact 失败时探测/重连（或对健康检查限速）。预计削减每键 IPC 延迟约 30-50%。

### TSF（每个应用进程内运行）

**1.7 `WeaselTSF` ↔ `CCandidateList` 循环引用：两个对象永久泄漏，`DllCanUnloadNow` 永远 S_FALSE** ✔ 代码核实
- `WeaselTSF.cpp:35`（`_cand = new CCandidateList(this)`，`_cand` 为 `com_ptr<CCandidateList>`，见 `WeaselTSF.h:220`）+ `CandidateList.h:82`（`com_ptr<WeaselTSF> _tsf`）
- 两侧互相持引用，引用计数永不到 0：每次 TIP 重建（切换输入法来回）泄漏一个实例（含 UI、DirectWrite 资源）；`~WeaselTSF` 不跑 → `DllRelease()` 不执行 → `g_cRefDll` 恒高。
- 修复：`CCandidateList::_tsf` 改为非持有裸指针（其生命周期严格嵌套于 `WeaselTSF`）。

**1.8 `Deactivate()` 从不调用 `_UninitThreadFocusSink()`** ✔ 代码核实
- `WeaselTSF.cpp:99-122`；`_UninitThreadFocusSink` 定义于 `:201`，全仓库无调用点
- 每个 Activate→Deactivate 循环都新增一次 `ITfThreadFocusSink` advise 且永不撤销：N 次切换后每次焦点变化触发 N 次注册表读 + N 次 `ProcessKeyEvent(0)` 管道往返 + N 次语言栏更新；`OnKillThreadFocus` 还会 N 次 `_AbortComposition`。
- 修复：在 `Deactivate()` 里补调用（另注：`:104` 与 `:113` 重复调用了两次 `_UninitThreadMgrEventSink`）。

**1.9 `CCandidateList::GetDescription` 返回同一个缓存的静态 BSTR → 宿主应用内 double-free/UAF** ✔ 代码核实
- `CandidateList.cpp:59-65`：`static auto str = SysAllocString(L"Candidate List"); *pbstr = str;`
- BSTR 出参契约是调用方负责 `SysFreeString`。第一个调用方 free 之后，后续所有调用返回悬垂指针；第二个调用方再 free 就是宿主应用内堆双重释放。同文件 `GetText`/`GetTooltipString` 都是正确的每次 `SysAllocString`。
- 修复：`*pbstr = SysAllocString(...)` 每次分配。

**1.10 服务端失联时的 detached 线程捕获 `this` → 宿主应用内 use-after-free** ✔ 代码核实
- `WeaselTSF.cpp:264-271`：`std::thread th([dir, this]() { ShellExecuteW(...); sleep 500ms; _Reconnect(); }); th.detach();`
- 仅在服务端死亡且连续 6 次重试失败时触发（恰是用户打字时）。宿主 500ms 内退出/释放 TIP 对象即 UAF。另 `static unsigned int retry`（`:237`）跨实例、跨线程共享且非原子，两个线程可同时 `retry>=6` 各拉起一个 start_service.bat（commit 47ed6b1 想修的"多开 WeaselServer"仍可能发生，见 2.1）。
- 修复：线程不捕获 `this`——只拉起进程，让下一次击键的常规重连路径收尾。

**1.11 `CEndCompositionEditSession::DoEditSession` 经 `_ClearCompositionDisplayAttributes` 间接解引用可能已置空的成员 `_pComposition`** ✔ 代码核实
- `Composition.cpp:94-117`（会话检查的是**会话自己的** `_pComposition`，`:102` 却调用服务成员版）+ `DisplayAttribute.cpp:10`（`_pComposition->GetRange(...)` 无空检查）
- `TF_ES_ASYNCDONTCARE` 会话可异步执行；期间 `WeaselTSF::_pComposition` 可能已被 `_FinalizeComposition()`（`Composition.cpp:425-427`，由 OnCompositionTerminated 或新的结束会话触发）置空 → 宿主应用内 AV。这正是 ef9c8b3 想修的崩溃类的残留。次生问题：若成员已指向**新的** composition，会把错误对象的显示属性清掉（行内 preedit 丢下划线）。
- 修复：把会话自己的 `ITfComposition*` 传入 `_ClearCompositionDisplayAttributes` 并加空检查。

**1.12 提交文本可能无声丢失：异步 start-composition 会话尚未执行时 `_InsertText` 拿到空 `_pComposition`** ✔ 代码核实
- `EditSession.cpp:21-37`（不处于合成时先 `_StartComposition` 再 `_InsertText`）+ `Composition.cpp:341-347`（`CInsertTextEditSession::DoEditSession` 在 `_pComposition == nullptr` 时 `return E_FAIL`）+ `Composition.cpp:367-380`（`_InsertText` 无条件返回 TRUE，失败完全静默）
- `TF_ES_ASYNCDONTCARE` 下若 TSF 异步执行 start 会话（Word 等持有文档锁时），insert 会话执行时 `_pComposition` 仍为空 → 提交文本永不插入，而 Rime 已清空 commit 缓冲 → **用户打的字消失**。8f2561f 引入的自动顶字上屏路径使该分支可被触达。
- 修复：insert 会话在无 composition 时改走 `ITfInsertAtSelection::InsertTextAtSelection`，或把插入链入 start 会话保证顺序。

### UI / 部署器 / 安装器

**1.13 候选高亮尺寸测量 `substr` 误用** ✅ 已本地复现（V1）
- `WeaselUI/StandardLayout.cpp:97-99`：`hilited_str = preedit.substr(_range.start, _range.end)` —— 第二参是**长度**不是结束索引。光标移入合成串中部（start>0）时高亮串吞掉尾部文本 → 高亮背景过长、后段文本出现空隙、窗口偏大。`WeaselPanel.cpp:642-644` 的死局部变量复制了同一错误。
- 修复：`substr(_range.start, _range.end - _range.start)`。

**1.14 `SyncUserData` 失败路径把服务端留在维护模式：全系统禁输直到手动重启** ✔ 代码核实
- `WeaselDeployer/Configurator.cpp:214-235`：`!rime->sync_user_data()` 时直接 `return 1`，跳过 `EndMaintenance()` 和后续恢复块。
- 修复：失败路径也要 `EndMaintenance()`（建议 scope-exit 保证）。

---

## 2. 中危问题（P2）

### TSF / 事件

**2.1 `_EnsureServerConnected` 的 `GetLastError() != ERROR_ALREADY_EXISTS` 永远判真** ✅ 已本地复现（V4）
- `WeaselTSF/WeaselTSF.cpp:244-262`：`GetLastError` 在 `m_client.Echo()` **之后**求值（管道调用覆盖 last error），create-mutex 的 183 永远丢失 → 47ed6b1 的互斥量防线失效，只剩进程快照兜底（且快照漏掉并发启动中的服务端）。
- 修复：`CreateMutex` 后立刻取 `GetLastError`。

**2.2 `_ProcessKeyEvent` 在 `keyCountToSimulate != 0` 时不写 `*pfEaten`，垃圾值写入跨实例共享的 static** ✔ 代码核实
- `KeyEventSink.cpp:7-9, 37-60`：注入 `VK_CAPITAL` 事件期间 `*pfEaten` 保持 TSF 缓冲的随机值，随后 `prevfEaten = *pfEaten` 存入文件级 static（`prevKeyEvent`/`prevfEaten`/`keyCountToSimulate` 三件套跨所有 WeaselTSF 实例与线程共享）→ 多 UI 线程应用里串扰、Caps Lock 物理状态可能与 Rime 认知漂移。
- 修复：函数开头无条件 `*pfEaten = FALSE;`；三个 static 改为成员。

**2.3 `ConvertKeyEvent` 使用函数级 static 缓冲，非线程安全** ✔ 代码核实
- `KeyEvent.cpp:44-51`：`static WCHAR buf[8]; static BYTE table[256];` 多 UI 线程主机下撕裂。顺带：`ToUnicodeEx(vkey, UINT(kinfo), ...)` 把整个打包 KeyInfo 当扫描码参数传（碰巧能工作）。改为栈局部即可。

**2.4 僵尸 UIElement：`Destroy()`/`DestroyAll()` 永不 `EndUIElement`** ✔ 代码核实
- `CandidateList.cpp:228-238`：`// EndUI();` 被注释掉，只隐藏窗口。合成中切 IME/应用 → `Deactivate` 调 `DestroyAll()` 时 `_uiStarted` 仍真 → `EndUIElement` 不会被调 → 元素残留在应用的 `ITfUIElementMgr` 里。
- 修复：`Destroy()/DestroyAll()` 内调用 `EndUI()`（幂等化）。

**2.5 `GetString` 的 BSTR 长度差一** ✅ 已本地复现（V3）
- `CandidateList.cpp:127`：`SysAllocStringLen(str.c_str(), str.size() + 1)` → `SysStringLen` 比实际多 1，按长度拷贝的应用会把 NUL 带进缓冲。

**2.6 TSF 卸载清理拼写错误 "Microsft"** ✔ 代码核实
- `Register.cpp:9`：`c_szTipKeyPrefix[] = "Software\\Microsft\\CTF\\TIP\\"` —— `UnregisterServer` 永远删不到真键 `Software\Microsoft\CTF\TIP\{CLSID}`，卸载残留 TIP 注册。
- 修复：改拼写；卸载后核验注册表。

**2.7 每线程管道句柄在线程退出时泄漏** ✔ 代码核实
- `include/PipeChannel.h:43-48`：`boost::thread_specific_ptr<HANDLE>` 只 `delete` 指针对象，从不 `CloseHandle`。用过 IME 的短命线程每个泄漏一个内核句柄，并在服务端钉住一个管道实例 + `_ProcessPipeThread` 工作线程直到客户端进程退出。
- 修复：TSS 存 RAII 包装（析构 `_FinalizePipe`）。

**2.8 `Transact` 忽略 `_Ensure()` 返回值 → 对 `INVALID_HANDLE_VALUE` 做 `WriteFile`** ✔ 代码核实
- `include/PipeChannel.h:120-125`：服务端死亡时靠"抛异常→catch 吞掉"兜底，每次首键多烧两次失败内核调用 + 一次无谓重连；`_Send` catch 内重试用的是**旧**句柄副本，重发必败。
- 修复：`if (!_Ensure()) throw ...;`；修正重试句柄。

**2.9 未知 session id 经 `map::operator[]` 静默插入默认项** ✔ 代码核实
- `include/RimeWithWeasel.h:88-93`：陈旧客户端 id 会插入全零 `SessionStatus` 并对 session 0 发起 5+ 次 rime 调用；`FindSession` 永不擦除 → map 缓慢无限增长。改 `find()` + 早退。

**2.10 响应解析 `vec[2]` 越界读 + IPC 异常弹 `MessageBoxA` 冻结宿主应用** ✅ 复现（V2）✔ 代码核实
- `WeaselIPC/ContextUpdater.cpp:52-64`（守卫 `< 2` 却读 `[2]`）；`WeaselIPC/Deserializer.h:8-16`（`TryDeserialize` catch 中 `MessageBoxA(NULL, "IPC exception", ...)` 在**打字应用**的输入线程弹模态框）；`ContextUpdater.cpp:73-76`（`text_wiarchive` 构造在 try 之外，构造抛出则直接炸出 ResponseParser）。
- 修复：`size() != 3` 检查；`MessageBox` 改日志；构造入 try；反序列化后校验 `candies.size()` 上限。

**2.11 64KB 收发缓冲静默截断** ✔ 代码核实
- `include/PipeChannel.h:163-166`：`data_sz > buff_size` 直接钳位；超长 body（长 `PREVIEW_ALL` + 长注释）截断后触发 2.10 的模态框。且无 body 时接收缓冲不清零，`GetResponseData` 可能拿旧字节（当前每键响应都带 body，属脆弱依赖）。

**2.12 服务端 UI 更新与 UI 线程 `DoPaint` 无锁竞争** ✔ 代码核实
- `RimeWithWeasel.cpp:521-556`（管道线程持 `g_api_mutex` 调 `m_ui->Update` → `WeaselPanel::Refresh`：重建 DWrite 资源/layout/窗口尺寸）vs `WeaselPanel.cpp:988+ DoPaint`（UI 线程）。WeaselUI 内无任何锁，`D2D1_FACTORY_TYPE_MULTI_THREADED` 只保护 D2D 内部。快速跨应用打字下替换中的 layout/pDWR 可能 UAF。
- 修复：`_UpdateUI` 在锁内只做快照，PostMessage 到 UI 线程完成一切 UI 变更（同时缩短全局锁临界区）。

**2.13 d73f629 残留：explorer.exe 特例每键新起线程 + 无锁读 `m_style`/`m_status`** ✔ 代码核实
- `RimeWithWeasel.cpp:73-87`：`Sleep(100)` + detached thread 调 `RequestRefresh()`（`WeaselTrayIcon.cpp:40-53` 中引用 UI 内部状态做快照）。PostMessage 机制落地后 100ms 垫片已无必要；快照读取与管道线程的写入构成数据竞争。该 commit 主体（`Shell_NotifyIcon` 移回消息线程、合并刷新、消息范围不冲突）是正确的。

**2.14 `Listen` 只捕获 `DWORD`，`boost::thread_resource_error` 直接 terminate 服务进程** ✔ 代码核实
- `WeaselServerImpl.cpp:421-434`：线程构造失败 → 异常逃出 → `std::terminate` → 所有应用退回原始输入；且线程构造抛出时管道句柄泄漏（只有 `catch(DWORD)` finalize）。改 `catch(...)`。

**2.15 `SecurityAttribute` 忽略 SDDL 转换失败** ⚠ 子代理报告
- `WeaselIPCServer/SecurityAttribute.cpp:61-73`：失败时 `pd==NULL` → 管道退回默认安全描述符 → UWP/AppContainer 客户端 `ERROR_ACCESS_DENIED`，且仅在这些应用里"输入法坏了"。`bInheritHandle=TRUE` 也不必要。

**2.16 无协议版本协商** ⚠ 子代理报告
- 管道名不含版本；原地升级的新旧 client/server 混搭会互相误解消息（而不是干净报错）。建议管道名带版本或首次 ECHO 交换元数据。

### UI

**2.17 `m_istorepos` 等成员未初始化即读** ✔ 代码核实
- `WeaselPanel.h:120`（构造函数初始化列表确认缺失），`:1265/1278` 才首次写；`DoPaint` 首帧即读（`:1002`），并通过 `UI::GetIsReposition()` 影响 TSF 侧 `KeyEventSink.cpp:31` 的上下反转判断。同样未初始化的还有 `m_offsetys` 等。C++17 起可直接 `bool m_istorepos = false;`。

**2.18 `init_font` 忽略 `wrap` 参数：preedit 换行配置永不生效** ✔ 代码核实
- `DirectWriteResources.cpp:98-136`：lambda 形参 `wrap` 从未使用，`:126` 恒用捕获的 `wrapping`；`pPreeditTextFormat`（`:136` 意图传 `wrapping_preedit`）实际拿到候选词的换行模式。设了 `style/max_width` 时 preedit 按整词而非逐字换行。

**2.19 `HR()` 在 `S_OK != hr` 时抛 `ComException`（含 S_FALSE），唯一 catch 在服务进程顶层** ✔ 代码核实（`WeaselUtility.h:308-321`）
- `WeaselPanel.cpp`/`DirectWriteResources.cpp` 绘制与布局路径大量 `HR()`：字体串坏、设备丢失、`S_FALSE` 返回 → 异常一路抛到 `WeaselServer.cpp` 顶层 try → **服务进程退出**，全局输入停止直到重启。`GetLayoutOverhangMetrics` 还会对可能为空的 `pTextLayout` 解引用。
- 修复：绘制路径改检查 + 降级（跳过绘制）；`HR` 只在 `FAILED(hr)` 时抛。

**2.20 每帧 `ModifyStyleEx`（= 每帧一次 `SWP_FRAMECHANGED` 的 `SetWindowPos`）** ✔ 代码核实
- `WeaselPanel.cpp:990`（`DoPaint` 首行）。样式只需设一次，移到 `OnCreate`。

**2.21 `EndDraw` 失败后仍把无文字帧送上分层窗口** ⚠ 子代理报告
- `WeaselPanel.cpp:1075-1078`：失败路径 `_InitFontRes(true); Refresh();` 后继续 `_LayerUpdate`，且 `Refresh` 因 `m_ctx==m_octx` 不会重绘 → 设备丢失时闪一帧空白。

### 部署器 / 安装器 / 更新

**2.22 `SwitcherSettingsDialog::OnOK` 的 `new[]` / `delete` 不配对** ✔ 代码核实
- `SwitcherSettingsDialog.cpp:161, 176, 180`：`new const char*[...]` 配 `delete selection`（标量 delete，UB）。改 `std::vector` 或 `delete[]`。

**2.23 `RimeSchemaList` 与 `UIStyleSettings` 泄漏** ⚠ 子代理报告
- `SwitcherSettingsDialog.cpp:20-23`（`get_available_schema_list`/`get_selected_schema_list` 结果从不 `schema_list_destroy`，每次"获取更多方案"都漏）；`UIStyleSettings.cpp:5-8`（`custom_settings_init` 无析构释放）。

**2.24 "获取更多方案"阻塞 UI 线程 + 未初始化 `HKEY` 传入 `RegCloseKey`** ✔ 代码核实
- `SwitcherSettingsDialog.cpp:114`（`HKEY hKey;` 未初始化）、`:155`（无论成败都 `RegCloseKey`）、`:149`（`WaitForSingleObject(cmd.hProcess, INFINITE)`：脚本控制台不关则对话框"未响应"）。另 `value[MAX_PATH]` 的 `RegQueryValueExW` 结果未强制 NUL 终止就参与字符串拼接。改 `RegGetValueW` + 带消息泵等待。

**2.25 备份用 `CP_ACP` 转换 rime 的 UTF-8 路径 + 100 字符栈缓冲收 `LB_GETTEXT`** ✔ 代码核实
- `DictManagementDialog.cpp:112`（中文用户名下同步目录变乱码，目录建错/选错）、`:121`（字典名 ≥100 字符栈溢出）。rime 返回 UTF-8，应 `u8tow`；缓冲 `MAX_PATH` 或先查长度。

**2.26 `OpenFolderAndSelectItem` 无条件 `CoUninitialize` 拆掉线程 STA** ✔ 代码核实
- `DictManagementDialog.cpp:13, 25`：`CoInitializeEx(MULTITHREADED)` 在 STA 线程返回 `RPC_E_CHANGED_MODE`（不计数），随后的 `CoUninitialize` 反而递减主循环的 STA 引用。`SUCCEEDED(hr)` 才配对。

**2.27 WOW64 重定向在 4 个错误路径上不恢复；`install()` 失败后继续跑** ✔ 代码核实
- `imesetup.cpp:177-243`：`Wow64DisableWow64FsRedirection` 后 `:180/195/213/222/236` 的提前 `return 1` 全部跳过 `:239` 的 Revert；`install()` `:386` 不检查 `install_ime_file` 返回继续注册流程——重定向失效状态下 `LoadLibrary("input.dll")` 可能解析到 64 位 DLL 致 TSF 注册被无声跳过。建议 RAII 包装重定向 + 失败即中止。

**2.28 regsvr32 退出码被忽略：TSF 注册失败仍报"安装成功"** ✔ 代码核实
- `imesetup.cpp:363-379`：`ShellExecuteExW` + wait 后不 `GetExitCodeProcess`，恒 `return 0`。

**2.29 卸载注册表清理不完整** ✔ 代码核实
- `imesetup.cpp:514-516`：只删 `HKLM\Software\Rime\Weasel`（且 `RegDeleteKey` 有子键即失败）；`HKCU\Software\Rime\Weasel`（`Profile`/`Hant`/`UpdateChannel`…）从不删除——本机即可观察到该键残留。旧值会让下次安装/卸载走错 profile（见 S3：`Hant` 对 hongkong/macau 传统方案恒 0，`Profile` 丢失时回退 `hans` 用错 LANGID）。

**2.30 `bump-version.ps1` 字符串比较版本号** ✅ 已本地复现（V5）
- `update/bump-version.ps1:177`：`0.9.x → 0.10.0` 会被当成"旧版本"拒绝。改 `[version]` 比较。

**2.31 测试套件签名过期：静默测试基类空实现** ✔ 代码核实
- `test/TestWeaselIPC/TestWeaselIPC.cpp:139-146`：`AddSession(LPWSTR)` / `FindSession(UINT)` vs 现行 `RequestHandler::AddSession(LPWSTR, EatLine)` / `FindSession(DWORD)`（`WeaselIPC.h:62-63`）——参数列表不同不再构成 override（无 `override` 关键字故编译不报错），测试实际只跑基类 no-op。修复签名后方可作为 1.1-1.3 的回归测试载体。

---

## 3. 低危 / 健壮性（P3）

| 位置 | 问题 | 状态 |
|---|---|---|
| `WeaselTSF.cpp:60`（DoEditSession） | 返回 `TRUE`(1) 而非 `S_OK`；`CStartCompositionEditSession::DoEditSession` 成功路径返回 `E_FAIL`（`Composition.cpp:25,59`，`hr=E_FAIL` 初始值未在成功分支覆盖） | ✔ |
| `Compartment.cpp:255-266` | OPENCLOSE 处理器内对自己监管的 compartment 调 `SetValue`——疑似重入（依赖 msctf 是否抑制同值通知），可能双次翻转/与用户"关闭键盘"意图打架 | ⚠ |
| `TextEditSink.cpp:73-118` | 一条 sink advise 失败（cookie 置 INVALID）时下轮清理被整块跳过，layout sink 注册泄漏；unadvise 后 cookie 不复位 | ⚠ |
| `KeyEventSink.cpp:86-100` | `_fTestKeyDownPending` 在应用吞掉后续 OnKeyDown 时永久挂起，下一个不同的键被无脑 `*pfEaten=TRUE` | ⚠ |
| `KeyEvent.cpp:51-55` | `ToUnicodeEx` 返回 -1（死键）/2（代理对）未处理，Rime 看不到死键字符；共享线程死键内部状态 | ⚠ |
| `KeyEvent.cpp:149-180` | 小键盘键不区分 NumLock 状态（ibus 以 MOD2_MASK 传 NumLock，此处从不设置） | ⚠ |
| `StandardLayout.cpp:6-12` | `swprintf_s<128>` 遇超长 label 触发 invalid-parameter 终止 | ⚠ |
| `WeaselUI.cpp:169-177` | 候选缩写 `substr` 可切开代理对（emoji 渲染成 U+FFFD） | ⚠ |
| `WeaselPanel.cpp:29-38` | 自定义皮肤图标加载失败被缓存，之后永不重试 | ⚠ |
| `RimeWithWeasel.cpp:1203-1211` + `DirectWriteResources.cpp:31-34` | `antialias_mode: force_dword`（yaml 合法值）映射为 `D2D1_TEXT_ANTIALIAS_MODE_FORCE_DWORD` 传给 `SetTextAntialiasMode` → `E_INVALIDARG` 被无视（V6 已证无枚举错位，此项为唯一残留） | ✔ |
| `RimeWithWeasel.cpp:27-31` | 会话 id 生成 `rbegin()->first + 1` 溢出 2³² 后回绕 0（理论） | ⚠ |
| `RimeWithWeasel.cpp:217-228` | 任一 `RemoveSession` 都清零 `m_active_session`，多客户端时触发多余 UI 刷新 | ⚠ |
| `RimeWithWeasel.cpp:395` 附近 | `get_state_label` 有 `RIME_API_AVAILABLE` 守卫，但 `select_candidate_on_current_page` 等三个新 API 未守卫——对旧 rime.dll 是空指针调用 | ⚠ |
| `WeaselIPCServer/WeaselServerImpl.cpp:293-300` | `PhysicalToLogicalPointForPerMonitorDPI` 每键 GetProcAddress 且不查空（靠 WeaselServer.cpp 的 Win8.1 gate 兜底）；应缓存 | ⚠ |
| `WeaselServer/WeaselService.cpp` | 死代码：`SERVICE_RUNNING` 后把 app 跑在 detached 线程，`Stop` 报 stopped 却不停线程；`_stoppedEvent` 创建即弃 | ⚠ |
| `WeaselIPC/WeaselClientImpl.cpp:44-46` | `Connect` 忽略 `ServerLauncher` 参数——TSF 传 NULL，死服务端只能靠 autorun 兜底，自动拉起能力实际丢失 | ⚠ |
| `WeaselTSF/dllmain.cpp:64` | `SetUnhandledExceptionFilter` 进程级**替换宿主应用**的崩溃过滤器，dump 后 `EXCEPTION_EXECUTE_HANDLER` 吞掉异常；应链式并 `EXCEPTION_CONTINUE_SEARCH` | ⚠ |
| `WeaselUtility.h:102-111` | `is_wow64()` 失败路径 `ExitProcess` —— 查询函数杀死宿主应用 | ⚠ |
| `WeaselDeployer.cpp:43-50` | 单实例互斥命中时静默 `exit(1)`，无任何提示 | ⚠ |
| `WeaselSetup.cpp:141-151` | `CustomInstall` detached 线程 `Sleep(500)` 序列，用户快速关掉消息框即被腰斩 | ⚠ |
| `imesetup.cpp:341-343` | `SetEnvironmentVariable` 失败抛 `std::runtime_error` 无人接 → `std::terminate` | ⚠ |
| `imesetup.cpp:482-486` 等 | 多处 `RegQueryValueEx` REG_SZ 未强制 NUL 终止（建议统一 `RegGetValueW`） | ⚠ |
| `SwitcherSettingsDialog::OnOK` count==0 路径 | 只 `delete; return 0;` 不 `EndDialog`——对话框留在打开状态（可用性怪异） | ✔ |
| `update/appcast.xml` | 无 `sparkle:edSignature`（当前未配公钥所以能跑）；一旦启用签名校验升级即断，建议提前加签 | ⚠ |
| 资源脚本 | `IDS_STR_ERR_UNKNOW/UNKNOWN` 同 143、`IDC_CHECK1/IDC_CHECK_INSTIME` 同 1006——重复 ID 属良性 | ⚠ |

**已验证无问题的点**（避免后人重复排查）：`DllCanUnloadNow` 哨兵计数逻辑自洽（问题是 1.7 的对象泄漏，不是计数数学）；五种 edit session 的引用计数配对；`OnEndEdit` 范围比较；`_EndComposition` 先弃所有权再 EndComposition 的顺序（同步场景下防自中止正确）；服务端对每个键事件必写响应体（客户端 `ERROR_MORE_DATA` 续读总能刷新缓冲）；UTF-8→UTF-16 游标换算两侧一致（代理对安全）；色彩解析 `_RimeGetColor` 边界处理完整；候选窗不抢焦点（`WS_EX_NOACTIVATE` + `MA_NOACTIVATE`）；d73f629 的 PostMessage 合并刷新主体正确。

---

## 4. 性能优化机会（按收益排序）

击键路径现状（每个 `OnTestKeyDown`）：`_IsKeyboardDisabled`（~8 次 COM）→ `GetKeyboardState` → **管道往返 #1 Echo** → **管道往返 #2 ProcessKeyEvent**（各含 `FlushFileBuffers`，服务端全程持 `g_api_mutex`）→ 编辑会话（响应解析 + 语言栏 compartment 读写 + `UpdateUIElement`）→ `_UpdateCompositionWindow` → `_UpdateUI`。

1. **去掉每键 `Echo()` 往返**（1.6）——两项内核往返减一，估 -30~50% 每键 IPC 延迟。
2. **删除 `FlushFileBuffers`**（1.2）——客户端不再等"服务端已读"，服务端不再持全局锁等"客户端已读"；既提速又消灭 1.2 的全局冻结类故障。估 -5~15% 每键开销。
3. **直通键早退**：`!*pfEaten && 无合成 && 无提交` 时跳过编辑会话/UI 全流程（TSF 侧 `KeyEventSink.cpp:95-99` + 服务端 `_Respond`/`_UpdateUI` 的 5 次 rime 交叉减到 2-3 次）。英文打字/游戏/终端受益最大，估服务端空闲键 -50%+ 工作量。
4. **语言栏/compartment 按变化更新**：`LanguageBar.cpp:252-265` 的 `OnUpdate` 现为无条件；`:402-418` 每键 compartment 读写；`GetIcon`（`:198-221`）对自定义图标**每次 `LR_LOADFROMFILE` 读盘**——按 key 缓存 HICON。
5. **UI 渲染三连**：(a) `_HighlightText`/`_DrawCandidates` 每帧为背景阴影 + 每个候选各分配整幅 `Gdiplus::Bitmap` 并跑 4 趟盒模糊（9 候选 @150% DPI ≈ 每帧 10 次全幅模糊）→ 按 (尺寸, 半径, 颜色) 缓存模糊位图，估阴影主题绘制提速 5-20×；(b) `GetTextSizeDW` 每次测量建 2 个 `IDWriteTextLayout`（每个 DoLayout 30-60 个）→ 合一，无换行时免第二个；(c) `CandidateInfo::operator==` 传值深拷贝（`WeaselIPCData.h:103`）改 `const&` + 每帧 memDC 位图按尺寸缓存。
6. **把 UI 刷新移出管道线程/全局锁**（2.12 的修复本身即优化）：多应用并发时消除队头阻塞。
7. **微项**：`_IsKeyboardDisabled` 用 compartment sink 缓存（每键省 ~8 次 COM）；候选序列化弃 `boost::text_woarchive` 换紧凑格式；缓存 `PhysicalToLogicalPointForPerMonitorDPI`；tray 状态未变时跳过 `_RefreshTrayIcon`；删掉 explorer.exe 每键开线程的特例。

---

## 5. 建议修复顺序

1. **一批小而关键的 IPC/服务端补丁**（风险收益比最高）：1.1 `ERROR_PIPE_CONNECTED`、1.2 删 Flush、1.3 body 偏移、2.8/2.9/2.14 的防御性修复。
2. **TSF 正确性**：1.7 循环引用、1.8 焦点 sink、1.9 BSTR、2.1 GetLastError、2.2 pfEaten、1.11/1.12 空指针与丢字、2.6 Microsft 拼写。
3. **线程模型清理**（1.4、1.5、2.12、2.13）：立一条规矩——handler 状态与 UI 只在 `g_api_mutex` 内或 UI 线程上触碰，debug 版用线程 id 断言强制。
4. **性能批次**（第 4 节 1-5 项）。
5. **复活测试**：先修 2.31 的签名，再以 TestWeaselIPC 为载体固化线格式（body 首行 `action=` 完整、535 不掉线、挂起客户端不拖死他人）与丢键回归。

---

## 附录 A：本地复现输出

复现程序：`%TEMP%\weasel_review\repro.cpp`（`cl /W4 /EHsc /MDd` 编译；不修改仓库任何文件）

```
[1] substr(pos, end) misuse in GetPreeditSize
  preedit="abcdefgh", highlight range [3,5)
  buggy substr(3, 5)   -> "defgh" (len 5)
  right substr(3, 2)   -> "de" (len 2)
  => reproduced: highlight measured too long

[3] SysAllocStringLen(size+1) off-by-one in GetString
  source len = 3, SysStringLen(BSTR) = 4
  => reproduced: BSTR length includes an extra NUL

[4] GetLastError() from CreateMutex clobbered by pipe calls
  after CreateMutex (already exists): 183 (ERROR_ALREADY_EXISTS=183)
  after intervening failing pipe call:  2 (ERROR_FILE_NOT_FOUND=2)
  => reproduced: ERROR_ALREADY_EXISTS already lost

[2] vec[2] read with vec.size()==2 (cursor=start,end)
  parsed 2 fields; guard (vec.size() < 2) -> continue
  [caught] CRT debug report: vector(1931) : Assertion failed: vector subscript out of range
```

PowerShell 版本比较（V5）：

```
PS> '0.10.0' -lt '0.9.0'          → True   （字符串比较：0.10.0 被当作更旧）
PS> [version]'0.10.0' -lt [version]'0.9.0' → False
```

W6 推翻依据（V6）：本机 SDK `um\d2d1.h` 中 `D2D1_TEXT_ANTIALIAS_MODE`：`DEFAULT=0, CLEARTYPE=1, GRAYSCALE=2, ALIASED=3`，与 `WeaselIPCData.h:196-202` 一一对应。

## 附录 B：构建信息

- `xmake -y`（release，MSVC 2022 / MT / LTCG）全 target 构建成功，39.3s。
- 测试 target（`TestWeaselIPC`/`TestResponseParser`）仅在 debug 模式纳入构建（xmake.lua:132-135）；TestWeaselIPC 使用与真实服务相同的管道名，为避免干扰正在运行的输入法（本机 WeaselServer PID 20736），本次仅做签名静态核对，未运行。

---

## 6. 2026-09-13 增补复查

3 路并行深审（TSF/UI、IPC/服务端、部署器/安装器），只收录未与上文重复的新发现；关键 P1 已逐条对照源码复核。

### 6.1 新增高危（P1）

**N1 `FindIME` 向 `RegCloseKey` 传未初始化句柄** ✔ 代码核实
- `WeaselTSF/Register.cpp:25-37`：`HKEY hSubKey;` 未初始化，`RegOpenKeyExW` 失败时跳过 if 块，但循环末尾无条件 `RegCloseKey(hSubKey)`；外层 `hKey`（:16）在 `RegOpenKeyExW` 失败时同样被 :40 无条件关闭。对栈垃圾值调用 `RegCloseKey` 是 UB，可能关闭宿主进程中恰好同值的无关句柄（如文件、管道）。

**N2 `_GetCompartmentDWORD` 释放未初始化指针，且成功路径返回 E_FAIL** ✔ 代码核实
- `WeaselTSF/Compartment.cpp:180-197`：`ITfCompartment* pCompartment;` 未初始化，`GetCompartment` 失败时 `:194 pCompartment->Release()` 照样执行 → 宿主应用内崩溃。同时 `hr` 初值 `E_FAIL`，`GetValue` 成功读到 `VT_I4` 后从未置 `S_OK`，所有调用方拿到的"成功"结果是失败 HRESULT。对照 `_SetCompartmentDWORD`（:199-213）有同样在 if 块外 `Release` 的模式（该处 GetCompartment 失败时同样崩溃）。
- 修复：`com_ptr` 或 `= nullptr` + 判空；成功路径 `hr = S_OK`。

**N3 `_Connect` 无限循环：服务端死亡时挂死宿主应用的输入线程** ✔ 代码核实
- `WeaselIPC/PipeChannel.cpp:41-45`：`while (_Invalid(pipe = _TryConnect())) ::WaitNamedPipe(name, 500);` 无退出条件。服务端永久消失（崩溃且无 autorun、卸载未重启）时，客户端 `Connect`/`_Ensure` 永不返回，TSF 输入线程被永久钉死，宿主应用表现为输入框假死。2.8 报告的"忽略 _Ensure 返回值"是另一面，此处是 `_Ensure` 自身根本不返回。

**N4 `OnEndSystemSession` 在消息线程无锁 Finalize：与管道线程竞争 rime 资源** ✔ 代码核实
- `WeaselIPCServer/WeaselServerImpl.cpp:99-108`：注销/关机消息里直接 `m_pRequestHandler->Finalize()`（内部 `rime_api->finalize()`）并置空指针。管道工作线程此刻可能正持 `g_api_mutex` 执行 `ProcessKeyEvent`——`g_api_mutex` 只覆盖管道路径，不覆盖窗口消息路径，rime 资源在使用中被销毁 → 注销时崩溃。`m_pRequestHandler` 的读写全程无同步，与 1.4 同根：主线程对 handler 的一切触碰都需要串行化。

**N5 `SelectCandidateOnCurrentPage` 不回传响应也不刷 UI：鼠标点选后提交文本延迟一击** ✔ 代码核实
- `RimeWithWeasel/RimeWithWeasel.cpp:312-320`：函数体只调 `rime_api->select_candidate_on_current_page`，与同类 `HighlightCandidateOnCurrentPage`/`ChangePage`（会 `_Respond`/`_UpdateUI`）不一致。点击候选产生的 commit 只能搭下一次按键的响应送达——点击上屏后用户看到窗口不消失、文本迟到。`CommitComposition`（:294-301）同样不 `_Respond`。

**N6 提权安装后 HKCU 写入落在管理员账户 hive** ⚠ 子代理报告（结构性缺陷，逻辑成立）
- `WeaselSetup/WeaselSetup.cpp:57-138`：标准用户经 `RestartAsAdmin` 提权后，`RimeUserDir`/`Profile`/`Hant` 等全部 `HKEY_CURRENT_USER` 写入落在**提权所用管理员账户**的 hive，真实用户的配置丢失。整进程提权 + HKCU 混写是结构性问题，需 `HKU\<真实用户SID>` 或推迟到用户会话写入。

**N7 bump-version.ps1 `replace_str` 用 `Out-File` 无 `-Encoding`：appcast.xml 变 UTF-16LE** ✔ 代码核实
- `update/bump-version.ps1:110`：Windows PowerShell 5.1 下 `Out-File` 默认 UTF-16LE，而 appcast.xml 声明 `encoding="utf-8"`；bump 版本后 WinSparkle 解析失败，**更新通道静默失效**。同函数 CHANGELOG.md 路径（:105）显式传了 `-Encoding UTF8`，此处漏了。

### 6.2 新增中危（P2）

**N8 "修改安装"切换 profile 不生效** ⚠ 子代理报告（与源码 `WeaselSetup.cpp:97` `if (!_has_installed) install(...)` 一致，已抽查确认该分支）
- 已安装状态下用户改选 profile（hans→hongkong），代码只写 HKCU 注册表（:124-138），从不调 `install()`，新 profile 的 TSF profile 注册/启用不执行。9d45db1 只修了卸载侧。

**N9 `/userdir:"..."` 引号不剥离，注册表写入带引号的非法路径** ⚠ 子代理报告（`WeaselSetup.cpp:207-210`）

**N10 `GetTextExtent` 会话每次按键泄漏一个 ITfRange** ⚠ 子代理报告
- `WeaselTSF/Composition.cpp:175-181`：`_pComposition->GetRange(&pRange)` 的 pRange 从未 Release，合成期间每个按键一次。

**N11 高亮索引无上限校验，裸数组越界读** ⚠ 子代理报告
- `WeaselTSF/CandidateList.cpp:158-161` `SetSelection` 不校验 nIndex；`WeaselPanel.cpp:315/862` 用 highlighted 调 `GetCandidateRect`，后者是容量 100 的裸数组下标（`StandardLayout.h`）。服务端返回异常值或宿主传任意值即越界读。

**N12 全屏自适应字号永久污染共享 pDWR** ⚠ 子代理报告
- `WeaselUI/FullScreenLayout.cpp:103-118`：`AdjustFontPoint` 反复改写共享 DirectWriteResources 的 TextFormat；回普通模式时 `_InitFontRes` 因样式/DPI 未变不重建，普通候选窗沿用被缩小的字号。

**N13 空 font_face 分割后直接 `[0]` 越界** ⚠ 子代理报告（`WeaselUI/DirectWriteResources.cpp:103-108`，`ws_split` 空串返回空 vector）

**N14 KeyEventSink 三件套 static 跨线程共享（补充 2.2）** ⚠ 子代理报告
- `WeaselTSF/KeyEventSink.cpp:7-9` 的 `prevKeyEvent`/`prevfEaten`/`keyCountToSimulate` 为文件级 static，多 UI 线程宿主下多个 WeaselTSF 实例互相踩踏；2.2 只覆盖了 `*pfEaten` 未写，此处是变量本身的归属错误。

**N15 WeaselPanel/UIImpl 用 static 变量存 this 的定时器机制** ⚠ 子代理报告（疑似）
- `WeaselPanel.cpp:449-461`、`WeaselUI.cpp:37,67-83`：`static UINT_PTR timer` 存对象指针，多实例时后建者覆盖，`OnTimer` 把错误指针 cast 回对象调用。

**N16 `CommitText` 前端错误信息在打字线程弹模态 MessageBox** ⚠ 子代理报告
- `WeaselTSF/WeaselTSF.cpp:13-20`：重连达到阈值时在输入线程弹 `MessageBox`，阻塞该线程全部输入；`static DWORD next_tick` 非线程安全且 GetTickCount 49.7 天回绕后判定反转。

**N17 `OnSetThreadFocus` 每次切窗口读注册表** ⚠ 子代理报告（`WeaselTSF.cpp:173-185`，热路径 I/O，与 2.x 语言栏同类）

**N18 服务端 `_Send` 失败重连逻辑误用客户端语义，泄漏连接且恢复无效** ✔ 代码核实（`WeaselIPC/PipeChannel.cpp:52-56, 151-175`）
- 服务端对自己的管道名执行 `CreateFile`（客户端逻辑），把新建客户端连接存入当前线程 `hpipe_ptr`，随后 `_WritePipe` 仍写给已断的旧 pipe：结果是泄漏一条客户端连接 + 恢复完全无效。与 2.8 重叠，此处补充"服务端侧根本不该走 `_Reconnect`"。

**N19 `OnNotify` 持锁调用可能重入的 rime 通知回调 → 疑似死锁** ⚠ 子代理报告（`RimeWithWeasel.cpp:383-406`，`m_notifier_mutex` 非递归，若 `get_state_label` 内同步发通知则自锁）

**N20 `_GetStatus` 对 `schema_name`/`schema_id` 未判空** ⚠ 子代理报告（`RimeWithWeasel.cpp:1446-1449`，NULL 构造 `std::string` 是 UB）

**N21 `_RefreshTrayIcon`/`_app_name` 的函数级 static 缓冲跨会话共享** ⚠ 子代理报告（`RimeWithWeasel.cpp:73-87, 627`，无锁 + 50 字节截断长进程名）

**N22 Configurator 短路链语义错误：方案选择取消则 UI 风格设置被静默跳过** ⚠ 子代理报告（`WeaselDeployer/Configurator.cpp:103-106`，`&&` 短路表达"两个都执行"）

**N23 `UpdateWorkspace` 不等部署完成即 EndMaintenance** ⚠ 子代理报告（疑似；`Configurator.cpp:141-154`，与 `SyncUserData` 的 `join_maintenance_thread` 处理不一致）

**N24 `UIStyleSettings` 每次打开配色设置泄漏一个 RimeConfig** ⚠ 子代理报告（`UIStyleSettings.cpp:15-38, 63`，`settings_get_config` 后从不 `config_close`）

**N25 xmake.lua 强制 O2：debug 构建无法调试；ARM64 安装器无构建入口** ⚠ 子代理报告
- `xmake.lua:39, 132-138`：优化 flags 无 release 守卫；WeaselSetup 仅 `is_arch("x86")` 构建，ARM64X 安装逻辑（imesetup.cpp）与构建系统脱节。

**N26 `UpdateWorkspace`/切换方案等动作缺响应体与查询通道（IPC 能力缺失）** ⚠ 子代理报告（`include/WeaselIPC.h:56-88`）
- 无异步通知推送通道（option/schema 提示只能搭按键响应，无按键即无提示）；无 `select_schema`/schema 查询 IPC；`SetOption` 无 get_option 回读。属第 5 节功能缺失的服务端根源。

### 6.3 新增低危（P3）

| 位置 | 问题 | 状态 |
|---|---|---|
| `WeaselTSF/Composition.cpp:25,59` | `CStartCompositionEditSession::DoEditSession` 成功路径返回 E_FAIL（已在上文 P3 表提及，此处确认影响：调用方无法区分真实失败） | ✔ |
| `WeaselTSF/EditSession.cpp:60` | 返回 `TRUE`(1) 而非 S_OK，HRESULT 语义混乱 | ⚠ |
| `WeaselTSF/Compartment.cpp:222-230` | `_InitCompartment` 第一个 Advise 的结果被第二个覆盖，OPENCLOSE advise 失败无感知 | ⚠ |
| `WeaselTSF/TextEditSink.cpp:78-86` | layout sink cookie unadvise 后不复位 | ⚠ |
| `WeaselTSF/TextEditSink.cpp:49-55` | `GetTextAndPropertyUpdates` 枚举后立即 Release，纯死代码 | ⚠ |
| `WeaselTSF/LanguageBar.cpp:186-191` vs `167-173` | `InitMenu` 不做本地化，与 `OnClick` 的 HANS/HANT 分支不一致；`GetActiveProfileLangId`（:122-135）死代码 | ⚠ |
| `WeaselUI/WeaselPanel.cpp:569-572` | `static Gdiplus::Bitmap* pBitmapDropShadow` 每次立即覆盖，static 无意义且线程不安全 | ⚠ |
| `WeaselUI/WeaselPanel.cpp:1182-1209` | sticky 阈值 50px、避让 6px 等魔法数字不按 DPI 缩放 | ⚠ |
| `WeaselUI/WeaselPanel.cpp:1122-1126` | `RedrawWindow` 遮蔽 CDoubleBufferImpl 基类 WM_PAINT 路径，双路径调 DoPaint | ⚠ |
| `WeaselUI/DirectWriteResources.cpp:31-34`、`WeaselPanel.cpp:194-195` | antialias_mode 越界值直传/强转给 SetTextAntialiasMode（上文 P3 表"force_dword"项的具体化） | ⚠ |
| `RimeWithWeasel.cpp:1031-1035` | `_RimeGetColor` 整型路径 `value > 0xffffffff` 经整型提升恒 false，死代码；负值被 `\| 0xff000000` 掩盖 | ⚠ |
| `WeaselServer/WeaselServer.cpp:41-46, 97-104` | `user_name[20]` 硬编码且失败不报错；重启重试仅 10×50ms，超时静默放弃（用户无输入法且无提示） | ⚠ |
| `WeaselServer/WeaselService.cpp:70` | `boost::thread{...}` 临时对象析构即 terminate（死代码路径，复活即炸） | ⚠ |
| `WeaselSetup/imesetup.cpp:32-59` | 占用文件改 `.old.0~9` + 延迟删除，重启后无人清理，System32 最多累积 10 份副本 | ⚠ |
| `WeaselSetup/imesetup.cpp:453-456` | DumpType=0 + CustomDumpFlags=0 生成几乎无信息的 dump，疑似与意图不符（通常 DumpType=1） | ⚠ |
| `WeaselDeployer/SwitcherSettingsDialog.cpp:120-155` | `RegCloseKey(hKey)` 无条件调用；`MAX_PATH+37` magic number；rime-install.bat 退出码不检查 | ⚠ |
| `WeaselDeployer/DictManagementDialog.cpp:61` 等 3 处 | 复制粘贴 `find_module("levers")->get_api()` 均不判空（levers 缺失即空指针） | ⚠ |
| `WeaselDeployer/DictManagementDialog.cpp:136-168` | 恢复词典后 `user_dict_list_` 不刷新 | ⚠ |
| `update/bump-version.ps1:9-16` | 失败回滚 `git checkout .` 会丢弃用户未提交改动，且对 untracked 无效 | ⚠ |

### 6.4 代码坏味道（新增）

1. **协议字符串手工拼接三处同步**：`RimeWithWeasel.cpp:741-938` `_Respond` 约 200 行手工 `append(L"\n")` 拼协议，新增 action 需同步改服务端、ResponseParser、Deserializer 三处，无编译期一致性检查——与"用枚举不用硬编码"规范直接相悖。
2. **UIStyle 70 字段三份手写清单**：`WeaselIPCData.h:427-536` 序列化 + `:365-423` `operator!=` 各一份，顺序已出现不一致（`vertical_auto_reverse`、`hover_type`），全靠人眼维护。
3. **按值传大对象**：`CandidateInfo::notequal(vector<Text>, vector<Text>)`、`Status::operator==(const Status)`、`_GenerateNewWeaselSessionId(SessionStatusMap sm)`（每次 AddSession 复制整个会话表，`RimeWithWeasel.cpp:27-31`）——应一律 const 引用。
4. **服务端硬编码 vim 习惯**：`RimeWithWeasel.cpp:264-292` Escape/Ctrl+c/Ctrl+[ 与 `(1 << 2)` 掩码写死在服务端，应属 rime 配置层。
5. **手工 new[] 转码**：`WeaselUtility.h:64-100` `string_to_wstring`/`wstring_to_string` 无异常安全，失败吞错；可直接 `std::wstring(len, 0)`。
6. **每键全量序列化**：style（约 70 字段）与 ctx.cand 每键经 text_woarchive 全量写入 64KB 管道体，无增量更新粒度——既是性能问题也是 N26 的结构根源。
7. **收发共用缓冲**：`PipeChannel.cpp:88-102` 第二次 ReadFile 直接复用与发送共用 offset 的 buffer，时序安全但零注释、零隔离。

### 6.5 功能缺失（作为输入法/输入法基础设施）

1. **深浅色主题感知**：WeaselUI/WeaselTSF 全目录无 `AppsUseLightTheme` 探测或监听，Windows 深色模式切换后候选窗不跟随（服务端有 `UpdateColorTheme` 管道动作但没有自动触发方）。✔ 代码核实
2. **失焦丢弃组合不恢复**：`OnSetFocus(FALSE)`/`OnKillThreadFocus` 直接 `_AbortComposition` 清空 preedit，切走再切回已输入编码丢失；ibus 按输入上下文保留。⚠
3. **WM_DPICHANGED 处理残缺**：`WeaselPanel.cpp:1164-1170` 收到 DPI 变化仅 `Refresh()`，不用 lParam 建议矩形重定位、不刷新 `m_inputPos`，跨 DPI 显示器拖动错位一轮；非 DPI-aware 宿主中无补救。⚠
4. **无 per-app 输入状态记忆**：ascii_mode 等为线程级全局，无按窗口记忆（如在 Word 英文、浏览器中文）。⚠
5. **截屏抠图不精确**：`WeaselPanel.cpp:247-275` `_CaptureRect` 从屏幕 DC 截取，会把遮挡候选窗的其他窗口一并截入，无多显示器负坐标/DPI 修正。⚠
6. **部署器管理能力残缺**：无配置 patch 编辑器（仅 UIStyleSettings 一处）、无 Rime 日志查看入口（安装时写了 DumpFolder/LogPath 但 UI 无处看）、`deploy()` 返回值被忽略、失败无用户可见诊断；升级无用户配置/已装方案迁移向导。⚠
7. **ARM64 构建链断裂**：xmake 无法产出 ARM64/ARM64X 安装器与 Deployer/Server，全靠 vcxproj 手工。⚠

### 6.6 增补复查中被推翻的项

- ~~"ARM64 24H2 卸载后 System32 残留 weaselx64.dll/weaselARM64.dll"~~：❌ 不成立。`imesetup.cpp:284-290` 中 x64/ARM64 的 `delete_file` 在 `if (is_arm64_machine())` 块内、`get_wow_arm32_system_dir` 分支**之外**，arm32 目录缺失不影响它们的执行。
- ~~"FontStoring.cpp 待审"~~：该文件已不存在于仓库。

### 6.7 增补建议修复顺序（并入第 5 节）

1. N1/N2（未初始化句柄/指针——宿主应用内崩溃类）与 N3（`_Connect` 加重试上限 + 报错路径）一批小补丁，风险收益比最高。
2. N4/N5 并入第 5 节第 3 条线程模型清理（主线程触碰 handler 一律投递）。
3. N7/N25 一行级修复（`-Encoding UTF8`、release 守卫）。
4. 坏味道 1/2/3 在下一次协议/UIStyle 变更时一并重构，避免继续三处同步。

---

## 7. P1 修复进度（2026-09-13 起）

| 问题 | 状态 | 提交 | 验证方式 |
|---|---|---|---|
| （基建）PipeServer 抽头文件 + TestPipeChannel 测试靶 | ✅ | `refactor(WeaselIPCServer)` | 冒烟往返测试通过（私有管道名，不影响运行中的 WeaselServer） |
| 1.1 `ERROR_PIPE_CONNECTED` 当致命错误掐断连接 | ✅ | `fix(WeaselIPC)` | TestPipeChannel 集成测试：客户端在 CreateNamedPipe 与 ConnectNamedPipe 之间连接 → 接受连接并可正常收发消息（旧代码此处抛 535 → Listen 断连） |
