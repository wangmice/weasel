# master 分支独立代码审查结果

## 审查对象与结论

- 审查日期：2026-10-24。
- 当前版本：`master@1e2993d21ccbfaaaca0ccd72aba85216826cbe46`。
- 比较基线：`release/0.17.5@88d581f3b24f81b5adf18df90157dc13d6339129`。
- 审查方式：只读源码快照审查、分子系统独立检查、隔离构建和测试；不沿用历史审查结论。
- 范围：TSF 输入与恢复、compartment/focus、IPC 与服务生命周期、UI/候选 COM 生命周期、安装部署及相关脚本。
- **结论：构建通过，但审查不通过，仍存在需要修复的阻塞问题。**

“新增”均指相对上述基线新增，不表示仅由 master 最后一次提交引入。下文路径与行号对应本次固定审查版本。

## 一、新增问题

### 1. P1：关闭服务时可能误取消引擎的文件 I/O

**位置**：`WeaselIPCServer/WeaselServerImpl.cpp:1070–1075`、`:1192–1199`。

**触发条件**：关闭服务时，`DrainWorkers()` 读取到 `pipe_io_pending=true`；在调用取消操作之前，worker 已完成管道读取、清除标志，并进入 librime 业务操作。

**问题与影响**：`CancelSynchronousIo()` 是线程级取消，不限定于先前的管道操作。因此可能取消同一 worker 随后执行的业务文件 I/O。原子标志只能描述采样瞬间，不能锁定线程所处阶段；`m_workers_mutex` 也没有约束 worker 从管道读取切换到业务处理的过程。

**证据**：

- 当前源码在读取原子标志后调用线程级 `CancelSynchronousIo()`，worker 的业务处理不处于同一同步边界。
- 相对基线的 diff 显示，这组标志与线程级取消逻辑为新增。
- 隔离 Win32 探针已观察到业务 `ReadFile` 返回 `ERROR_OPERATION_ABORTED(995)`：`business_ReadFile_error=995 pending_after_sample=0 cancellation_hit_business=1`。

**证据边界**：这是取消对象越界的源码与隔离探针证据，不是完整生产引擎数据损坏复现。

**修复方向**：使用操作级取消，或建立能保证取消期间 worker 不进入业务 I/O 的阶段交接/确认机制；不能仅重复读取标志。

### 2. P1：部署/同步的硬超时仍覆盖非原子数据写入

**位置**：`WeaselDeployer/Configurator.cpp:258–300`、`:509–517`；`WeaselDeployer/WeaselDeployer.cpp:21–31`。

**触发条件**：部署或同步耗时达到硬超时，watchdog 在配置覆写、同步合并或词库迁移尚未结束时终止进程。

**问题与影响**：watchdog 到期直接 `TerminateProcess()`，但覆盖的 `deploy()` 包含 `installation_update`、用户词库升级；同步也包含配置写入和词库合并。固定版本 librime 中，配置文件直接通过 `std::ofstream` 覆写；旧词库升级存在“备份 → 删除旧库 → 恢复”的连续步骤。超时落在这些窗口时，可能留下截断配置或未完成迁移。

把同步移到子进程能隔离执行，但不会自动使数据写入具备事务性。

**证据**：除调用方源码外，对照仓库固定的 librime revision `49be0d491e88f8d779c9c3456dcf603b67e077b1`：

- `librime/src/rime_api_impl.h:118–123`：部署执行 installation update、workspace update、user dictionary upgrade。
- `librime/src/rime_api_impl.h:138–144`：同步调度 installation update、配置备份和用户词库同步。
- `librime/src/rime/config/config_data.cc:84–95`：通过 `std::ofstream` 直接保存配置。
- `librime/src/rime/lever/user_dict_manager.cc:172–175`：备份、关闭、删除旧库后执行恢复。

**证据边界**：源码链路确认，未对真实用户数据做破坏性实验；不声称已复现实际用户数据损坏。

**修复方向**：将可硬终止操作限制在可恢复、事务化的边界；非原子写入需临时文件与原子替换、持久化恢复标记或明确的迁移恢复流程。

### 3. P2：watchdog 撤防没有等待确认，完成后仍可能被旧线程终止

**位置**：`WeaselDeployer/WeaselDeployer.cpp:155–165`、`:23–31`。

**触发条件**：watchdog 已从等待函数取得 `WAIT_TIMEOUT`，但尚未调用终止；此时业务操作完成并调用 `Disarm()`。

**问题与影响**：`Arm()` 关闭并丢弃线程句柄，`Disarm()` 只设置事件并立即返回。设置事件不会改变 watchdog 已取得的等待结果，因此旧线程仍会调用 `TerminateProcess()`。撤防返回不代表终止动作已不可能发生。

**证据**：当前线程与事件所有权、等待结果分支及 `Disarm()` 方法体；纯逻辑交错诊断确认“超时先返回、随后撤防”仍会进入终止分支。该诊断不是实际运行 15 分钟后的生产端到端复现。

**修复方向**：建立线性化的撤防/超时决策，并保留线程句柄等待完成确认；避免将一个未确认停止的 watchdog 留到下一阶段。

### 4. P2：成功停止卡在 Starting 的旧服务后，重启分支反而退出

**位置**：`WeaselServer/WeaselServer.cpp:326`、`:346–373`。

**触发条件**：非 `/q` 的正常重启；首次 `client.Connect()` 成功，旧服务仍处于 Starting 并持有生命周期锁；`ForceStopStartingServer()` 成功停止旧服务。

**问题与影响**：首次连接结果保存为 `const bool connected`。恢复分支断开连接、清空旧服务进程句柄后，仍进入后面的 `if (connected)`；重新查找已经退出的旧服务失败，返回 `1`，没有进入替代服务的创建流程。此次重启只完成停止，需再次启动。

**证据**：

- 当前源码中旧连接状态在停止旧服务后没有更新。
- 相对基线新增的 Starting 恢复分支与旧分支组合导致该路径。
- 隔离分支探针输出：`connected_starting_restart result=1 replacement_reached=0 rediscovery=1 observed_bug=1`。
- 对照分支输出：`disconnected_starting_restart result=99 replacement_reached=1 control=1`；`99` 是探针替代服务入口的标记，不是产品返回码。

**证据边界**：探针使用无副作用的服务发现/停止替身，未启动或停止真实服务。

**修复方向**：成功停止旧 generation 后更新连接状态并直接进入替代服务启动流程；补充“pipe 已就绪但 Starting 未完成”的重启测试。

## 二、仍存在的既有问题

以下相对本次基线已经存在，不应计作新增回归，但也不能因构建通过而忽略。

### 5. 进程退出监听的发布竞态

**位置**：`WeaselIPCServer/ClientProcessTracker.h:113–134`；退休路径 `:217–270`。

退出回调可在 `WatchExit()` 返回前将 entry 退休，退休线程随即销毁仍在注册过程中的 `Process`。新建 watch 尚未发布到 entry，entry 的析构无法通过该 watch 等待注册过程结束。

隔离探针观察到：`early_callback_diagnostic process_destroyed_inside_WatchExit=1 token_returned=0`；普通对照中该销毁标志为 `0`。该文件与基线相同，因此属于既有生命周期缺陷。探针证明了所有权交错，不等同于复现真实 Windows 注册等待崩溃。

### 6. UI-less 候选确认不提交

**位置**：`WeaselTSF/CandidateList.cpp:196–211`。

宿主通过候选 COM 行为接口调用 `SetSelection(n); Finalize()` 时，前者只改本地高亮，后者只关闭 UI，没有向引擎选词或提交目标文本，却返回成功。此结论限于该接口路径，不表示普通自绘候选鼠标选词都失败。

证据为导出接口、方法体及普通选词路径的调用差异；没有真实宿主原生复现。基线具有相同逻辑。

### 7. 集成候选接口吞键但不处理

**位置**：`WeaselTSF/CandidateList.cpp:232–240`。

有 owner 时，`ITfIntegratableCandidateListUIElement::OnKeyDown()` 返回 eaten，但没有导航、确认、翻页或按键处理调用。使用该接口且遵循 eaten 返回值的宿主会停止自己的处理，输入法也没有执行相应动作。

证据为完整方法体及导出的接口契约；没有实际搜索框宿主复现。基线同样存在。

### 8. 焦点退出仍可能同步阻塞约三秒

**位置**：`WeaselTSF/WeaselTSF.cpp:237–243` → `WeaselTSF/Composition.cpp:938–941` → `WeaselIPC/WeaselClientImpl.cpp:229–234`。

`OnKillThreadFocus()` 经 `_AbortComposition()` 同步发送 `ClearComposition`。当 client 本地仍认为 session 活跃而服务端不回复时，使用的是 3000ms state-changing 预算，不是短焦点通知预算；即使没有本地 composition，也会先尝试该调用。

这是有界 UI stall，不是已证明的无限等待或互斥锁死锁。源码预算与可达调用链已确认，未完成该完整焦点回调的私有服务计时复现。基线同样可达。

## 三、构建与测试结果

| 验证项 | 实际结果 |
|---|---|
| x64 Debug 快照构建 | 通过 |
| x86 Debug 快照构建 | 通过 |
| x64 Release 快照构建 | 通过 |
| x86 Release 快照构建 | 通过 |
| 两种架构的原生测试执行 | 28 次，26 通过，2 失败 |
| 源码策略检查 | 20/20 通过 |
| PowerShell 语法解析 | 30 个脚本通过 |
| 安装结果隔离测试 | 13/13 通过 |

### 原生测试失败的归类

两次失败均为 x64/x86 的 `TestResponseParser`，断言位置 `test/TestResponseParser/TestResponseParser.cpp:125`：旧断言要求丢弃只有 start/end、没有 cursor 的属性，与当前“cursor 字段可选”的兼容逻辑不一致。

基线版本的同项测试通过。当前失败归类为**测试契约未同步**，不是已确认的产品解析错误。应更新测试以验证可选 cursor 的正确默认值，同时保留真正截断数据的拒绝断言。

### 安装测试夹具失败

`test/TestInstaller/test_arm64_payload.py` 编译生成的 NSIS 夹具失败，出现 `StrCpy` 用法错误。提取的初始化块引用生产变量，而夹具缺少相应声明；基线同项也失败。它是既有测试夹具/覆盖问题，不能作为本次新增产品缺陷，也不能声称 ARM64 安装矩阵已通过。

### 构建条件

- 使用固定源码 archive 在 scratch 构建，不使用工作树旧二进制作为当前源码证据。
- 使用 xmake、VS2022/v143、现有 Boost 1.92.0；`BOOST_ROOT=D:/src/weasel/deps/boost_1_92_0`。
- archive 不含忽略的 Rime API 头文件和导入库，构建时从工作树补入这些现有非秘密前置输入，记录来源与 SHA-256。因此结果是**源码快照构建**，不是无需额外输入的 clean checkout 构建。
- 初次 x86 构建出现编译器/链接目标架构不一致；显式指定对应架构工具、清理并重建后通过。Release 构建在对应 vcvars 环境中执行。
- 已读取生成 PE 的 machine 字段，核对 x64 为 `0x8664`、x86 为 `0x14c`。
- 源码策略 PASS 只表示配置/源码断言通过，不替代生命周期与竞态行为测试。

## 四、覆盖边界与未完成项

1. 没有执行真实安装、卸载、输入法注册、生产服务启动/停止或生产 IPC 测试。
2. 未对真实用户数据进行写入或破坏性故障注入；数据损坏风险按源码链路报告。
3. 未完成 ARM 原生运行、真实宿主组合、完整安装/升级/回滚验收、多显示器混合 DPI 和驱动异常矩阵。
4. `TestServerCommands` 使用禁止真实服务启动的测试配置，不能为完整生产恢复 worker 的副作用提供端到端覆盖。
5. 部分 IPC/安装子审查完整 Markdown 报告落盘失败；已恢复的探针结果与当前源码用于复核上述条目，未恢复内容不作为已确认结论。本报告不是全范围无遗漏认证。
6. 子审查另有 TSF/UI 候选问题，完整记录保留在下列已存在的子报告中；未逐项纳入本次对用户交付的已复核阻塞清单，不能仅凭子报告标签提升为父审查确认项。
7. 本报告所述“探针退出 0”可能表示成功观察到缺陷交错，并不表示产品回归测试通过。

## 五、证据目录

本次固定快照、构建日志和测试输出目录：

`C:/Users/wangmice/AppData/Local/hermes/cache/scratch/weasel-review-20261024-003752/`

主要记录：

- `manifest.json`：固定 revision、baseline、初始工作树状态、子模块版本、工作树受跟踪文件哈希。
- `build-inputs.json`：补入构建前置输入的来源和哈希。
- `build-x64-debug.log`、`build-x86-explicit.log`：成功的 Debug 构建日志。
- `build-x64-release.log`、`build-x86-release.log`：Release 构建日志。
- `verification-x64.json`、`verification-x86.json`：各项测试退出码、耗时和日志路径。
- `baseline-run-parser.log`：基线解析器测试结果。
- `test_install_result.py.log`、`test_arm64_payload.py.log`、`baseline-arm64-payload.log`：安装隔离测试与基线对照结果。
- `binary-architectures.json`：生成文件 PE 架构记录。
- `ipc-callback-probe-7e56/`：隔离回调、取消与重启分支探针二进制；重执行时观察了各进程最终退出状态。
- `installer-audit-independent-probes.py`：安装/部署纯逻辑诊断，断言成功不代表产品无缺陷。
- `tsf-input-focus-compartment-audit-1e2993d.md`：TSF 子审查完整记录。
- `ui-candidate-rime-independent-1e2993d.md`：UI/候选 COM/Rime 子审查完整记录。

注意：scratch 是缓存目录，不保证长期保留；本仓库内报告保存结论，不能替代日志与二进制的长期归档。

## 六、建议处理顺序与最终状态

1. 优先修复关闭时线程级取消跨越到业务 I/O 的竞态。
2. 修正部署/同步硬终止的数据安全边界及 watchdog 撤防协议。
3. 修复 Starting 服务恢复后的替代启动分支，并加入确定性回归测试。
4. 修复既有的进程退出监听发布竞态与候选 COM 行为接口问题。
5. 同步解析器测试契约、修复 ARM64 安装测试夹具后重新验收。

审查结束与保存报告前复查：HEAD 仍为 `1e2993d21ccbfaaaca0ccd72aba85216826cbe46`，受跟踪内容与审查开始时一致，保存前工作树干净。此次只新增本报告，未修改产品源码、未提交或推送。
