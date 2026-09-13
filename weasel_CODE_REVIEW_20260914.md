# Weasel 代码审查与修复跟踪报告（2026-09-14）

- **基线**：HEAD = `acf9c6b`，工作区干净（仅未跟踪 `.vscode/`）
- **方法**：两路并行深度静态审查（A 路：WeaselTSF / WeaselServer / WeaselSetup / WeaselDeployer / test；B 路：WeaselIPC / WeaselIPCServer / RimeWithWeasel / WeaselUI / include 自研头文件），对照旧报告 `weasel_CODE_REVIEW_20260906.md` 与其后约 25 个修复 commit 剔除已修复项；随后逐项本地验证、分批修复。
- **状态标记**：验证列 ✅ 已本地复现 / ✔ 代码核实 / ⚠ 未能完全验证 / ❌ 被推翻 / —（非 bug 无需验证）。修复列 未修复 / 🔧 批次N / ✅ 已修复（commit）。

## 0. 总索引

### A 路新发现

| 编号 | 级别 | 类型 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|---|
| A1 | P2 | bug | WeaselTSF/TextEditSink.cpp:35-44 | OnEndEdit 泄漏 GetSelection 返回的 ITfRange（合成期每击键一次） | 待验证 | 未修复 |
| A2 | P2 | bug | WeaselTSF/CandidateList.cpp:230-234,289-291 | Destroy 不清 _uiStarted，StartUI 早退 → 本组合期候选窗永久丢失 | 待验证 | 未修复 |
| A9 | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘快照在管道线程读 UI style_/status_（wstring）数据竞争（= B2，合并处理） | 待验证 | 未修复 |
| A3 | P3 | bug | WeaselTSF/CandidateList.h:85 | uiid 未初始化即传入 UpdateUIElement | 待验证 | 未修复 |
| A4 | P3 | bug | WeaselTSF/Compartment.cpp:75-89 | _Unadvise 对 null _compartment 解引用；_cookie 未初始化 | 待验证 | 未修复 |
| A5 | P3 | bug | WeaselTSF/DisplayAttribute.cpp:38-39 | 空 range 时对可能 null 的 _pComposition 解引用（潜在） | 待验证 | 未修复 |
| A6 | P3 | bug | WeaselTSF/WeaselTSF.h:239, WeaselTSF.cpp:152 | _gaDisplayAttributeInput 未初始化且初始化失败被忽略 | 待验证 | 未修复 |
| A7 | P3 | bug+perf | WeaselTSF/LanguageBar.cpp:403-419 | 每键无条件读写 compartment；读取失败回写会清掉无关转换位 | 待验证 | 未修复 |
| A8 | P3 | 死代码 | WeaselTSF/WeaselTSF.cpp:13-20 | error_message（模态框+非线程安全 static）无调用者 | 待验证 | 未修复 |
| A10 | P3 | bug | WeaselServer/WeaselTrayIcon.cpp:22-38 | 栈上 CIcon 句柄存入 m_tnd.hIcon 后悬垂 | 待验证 | 未修复 |
| A11 | P3 | bug | WeaselServer/SystemTraySDK.cpp:427-439 | SetIconList(HICON*,UINT) 差一越界（无调用者） | 待验证 | 未修复 |
| A12 | P3 | bug | WeaselServer/SystemTraySDK.cpp:823-832,694-697 | 菜单句柄泄漏 / 子菜单双重销毁 | 待验证 | 未修复 |
| A13 | P3 | bug | WeaselSetup/WeaselSetup.cpp:94-111 | /i 流程取消选项对话框仍继续安装；_has_installed 过期 | 待验证 | 未修复 |
| A14 | P3 | bug | WeaselSetup/WeaselSetup.cpp:68-76 | 注册表字符串未强制 NUL 终止即构造 wstring | 待验证 | 未修复 |
| A15 | P3 | perf | WeaselTSF/EditSession.cpp:8-14 | 每击键堆分配 shared_ptr<Context>+Config+parser | — | 未修复 |
| A16 | P3 | bug | WeaselDeployer/UIStyleSettings.cpp:42-58 等 | 预览路径用 ACP 解码 UTF-8，非 ASCII 用户名下必失败 | 待验证 | 未修复 |

### A 路：旧报告已知且确认仍未修复（K 系列）

| 编号 | 级别 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|
| K1 | **P1** | WeaselDeployer/Configurator.cpp:220-228 | SyncUserData 失败不调 EndMaintenance → 服务端永久维护态、全系统禁输 | 待验证 | 未修复 |
| K2 | P2 | WeaselTSF/KeyEventSink.cpp:7-60 | static 三件套跨实例/线程共享；pfEaten 未写即存 static | 待验证 | 未修复 |
| K3 | P2 | WeaselTSF/KeyEvent.cpp:44-51 | ConvertKeyEvent 函数级 static buf/table 非线程安全；扫描码传参错误 | 待验证 | 未修复 |
| K4 | P2 | WeaselTSF/CandidateList.cpp:129 | SysAllocStringLen(size()+1) BSTR 长度差一 | 待验证 | 未修复 |
| K5 | P2 | WeaselTSF/Register.cpp:10,226-231 | "Microsft" 拼写 + HKCR 下清理对真实 TIP 键结构上无效 | 待验证 | 未修复 |
| K6 | P2 | WeaselDeployer/SwitcherSettingsDialog.cpp:161 等 | new[] 配标量 delete（UB） | 待验证 | 未修复 |
| K7 | P2 | SwitcherSettingsDialog.cpp:20-23; UIStyleSettings.cpp:5-8 | schema list / settings 无对应 destroy | 待验证 | 未修复 |
| K8 | P2 | SwitcherSettingsDialog.cpp:114-155 | 未初始化 HKEY、无条件 close、INFINITE 等待、无 NUL | 待验证 | 未修复 |
| K9 | P2 | WeaselDeployer/DictManagementDialog.cpp:109-123 | CP_ACP 解码 UTF-8 + LB_GETTEXT 缓冲可溢出 | 待验证 | 未修复 |
| K10 | P2 | DictManagementDialog.cpp:13,25 | STA 线程无条件 CoUninitialize 拆主循环计数 | 待验证 | 未修复 |
| K11 | P2 | WeaselSetup/imesetup.cpp:178-464 | WOW64 重定向 4 处提前 return 不恢复；install() 忽略文件拷贝结果 | 待验证 | 未修复 |
| K12 | P2 | imesetup.cpp:364-375 | regsvr32 退出码不检查，失败仍报成功 | 待验证 | 未修复 |
| K13 | P2 | imesetup.cpp:514-517 | 卸载不清 HKCU 配置；RegDeleteKey 有子键即失败 | 待验证 | 未修复 |
| K14 | P2 | WeaselSetup/WeaselSetup.cpp:109-111 | 改 profile 只写注册表不重注册 TSF profile | 待验证 | 未修复 |
| K15 | P2 | WeaselSetup/WeaselSetup.cpp:209-212 | /userdir 引号不剥离 | 待验证 | 未修复 |
| K16 | P2 | WeaselTSF/Composition.cpp:163,166-182 | GetTextExtent 会话泄漏 pRange 与 selection.range（每击键） | 待验证 | 未修复 |
| K17 | P2 | WeaselTSF/CandidateList.cpp:160-163 | SetSelection 不校验 nIndex（下游裸数组越界，= B9 同族） | 待验证 | 未修复 |
| K18 | P2 | WeaselDeployer/Configurator.cpp:103-106 | && 短路：取消方案对话框静默跳过 UI 风格设置 | 待验证 | 未修复 |
| K19 | P2 | Configurator.cpp:141-155 | deploy 后不 join_maintenance_thread 即 EndMaintenance | 待验证 | 未修复 |
| K20 | P2 | test/TestWeaselIPC/TestWeaselIPC.cpp:143-146 | AddSession 签名不 override，测试服务端会话计数不增长 | 待验证 | 未修复 |
| K21 | P3 | WeaselTSF/WeaselTSF.cpp:177-190 | 每次线程焦点切换读注册表 + 2 次 IPC 往返 | — | 未修复 |
| K22 | P3 | 多处 | P3 杂项族（详见 A 路报告 §3 表） | 待验证 | 未修复 |
| K23 | P3 | perf | 每键 compartment/语言栏/图标读盘等性能族 | — | 未修复 |
| K24 | P3 | WeaselTSF/KeyEventSink.cpp:65-74 | 失焦即清空已输入编码，切回不恢复 | 待验证 | 未修复 |

### B 路发现

| 编号 | 级别 | 类型 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|---|
| B1 | **P1** | bug | WeaselUI/WeaselUI.cpp:50-94 等 | Show/Hide/ShowWithTimeout 未 marshal，管道线程持 g_api_mutex 跨线程 ShowWindow → 与消息线程互等死锁 | 待验证 | 未修复 |
| B2 | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘刷新在管道线程读 ui.style_/status_（= A9） | 待验证 | 未修复 |
| B3 | P2 | bug | WeaselUI/StandardLayout.cpp:98 | substr(start,end) 第二参误当长度（旧 V1 已复现，此处漏修） | 待验证 | 未修复 |
| B4 | P2 | bug | WeaselIPC/ContextUpdater.cpp:55-62 | 守卫 size()<2 却读 vec[2] 越界（旧 V2） | 待验证 | 未修复 |
| B5 | P2 | bug | WeaselIPC/Deserializer.h:8-16 | 反序列化异常在输入线程弹模态 MessageBox | 待验证 | 未修复 |
| B6 | P2 | bug | include/PipeChannel.h:64-67 | TSS 管道句柄退出只 delete 不 CloseHandle | 待验证 | 未修复 |
| B7 | P2 | bug | WeaselUI/DirectWriteResources.cpp:103-106 | font_face 空串时 ws_split[0] 越界（MSVC 空 vector） | 待验证 | 未修复 |
| B8 | P2 | bug | include/WeaselUtility.h:315-321 等 | HR() 对 S_FALSE 也抛且 UI 路径无局部 catch → 服务整体退出 | 待验证 | 未修复 |
| B9 | P2 | bug | WeaselUI/VerticalLayout.cpp:215 等 | highlighted 无上限校验直接索引裸数组 | 待验证 | 未修复 |
| B10 | P2 | bug | WeaselUI/WeaselPanel.h:158-162 | m_istorepos/m_offsetys 等未初始化即读 | 待验证 | 未修复 |
| B11 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp | 每键 ~7 次 rime 交叉：直通键无早退、get_status×2、get_property 每键 | — | 未修复 |
| B12 | P2 | perf | WeaselUI/WeaselPanel.cpp 等 | 每键整窗重算重绘：布局重建、双 layout、全幅模糊 | — | 未修复 |
| B13 | P2 | perf | include/WeaselIPCData.h:103,166 | notequal/operator== 按值深拷贝候选向量（每键 6 份） | — | 未修复 |
| B14 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp:73-87 | explorer.exe 每键 detached 线程 + Sleep(100) | — | 未修复 |
| B15 | P3 | bug | WeaselIPC/WeaselClientImpl.cpp:145-191 | StartSession 失败 body 残留，下次拼双份客户端信息 | 待验证 | 未修复 |
| B16 | P3 | bug | include/PipeChannel.h:171-184 | body>64KB 时 failbit → 静默只发头不发 body | 待验证 | 未修复 |
| B17 | P3 | bug | WeaselIPCServer/WeaselServerImpl.cpp:445-461 | Listen catch(...) 后无退避，管道创建持续失败时 100% CPU | 待验证 | 未修复 |
| B18 | P3 | bug | WeaselServerImpl.cpp:450-519 | worker 先于 _RegisterWorker 结束 → m_workers 残留已关闭句柄 | 待验证 | 未修复 |
| B19 | P3 | bug | WeaselUI/WeaselPanel.cpp:1261-1264 | MoveTo marshal 不检查 PostMessage 返回值泄漏 RECT | 待验证 | 未修复 |
| B20 | P3 | bug | WeaselIPC/Configurator.cpp:17-21 | 守卫检查 p_context 却解引用 p_config | 待验证 | 未修复 |
| B21 | P3 | bug | WeaselIPC/Deserializer.cpp:13-28 | s_factories 无锁懒初始化 | 待验证 | 未修复 |
| B22 | P3 | bug | RimeWithWeasel.cpp:549 等 | operator[] 向会话表插入死条目 | 待验证 | 未修复 |
| B23 | P3 | bug | StandardLayout.cpp:6-12 等 | swprintf_s 超长/非法格式符 → CRT 直接终止进程 | 待验证 | 未修复 |
| B24 | P3 | bug | WeaselUI/WeaselPanel.cpp:1003 | DoPaint 每帧 ModifyStyleEx | 待验证 | 未修复 |
| B25 | P3 | bug | WeaselPanel.cpp:1088-1091 | EndDraw 失败仍送无文字帧 | 待验证 | 未修复 |
| B26 | P3 | bug | DirectWriteResources.cpp:98-136 | init_font 忽略 wrap 形参，preedit 换行失效 | 待验证 | 未修复 |
| B27 | P3 | bug | FullScreenLayout.cpp:68-123 | AdjustFontPoint 永久污染共享字号 | 待验证 | 未修复 |
| B28 | P3 | bug | WeaselServerImpl.cpp:307-315 | 每键 GetProcAddress 且不判空 | 待验证 | 未修复 |
| B29 | P3 | perf | RimeWithWeasel.cpp:27-31 | 会话表按值拷贝 | — | 未修复 |
| B30 | P3 | bug | RimeWithWeasel.cpp:1462-1463 | schema_name/id 未判空构造 std::string UB | 待验证 | 未修复 |
| B31 | P3 | bug | include/WeaselUtility.h:14-32 | getUsername 二次调用失败未校验 | 待验证 | 未修复 |
| B32 | P3 | bug | RimeWithWeasel.cpp:177 | create_session 返回 0 未检查全链路静默失败 | 待验证 | 未修复 |
| B33 | P3 | bug | RimeWithWeasel.cpp:394-417 | 非递归互斥自锁风险（待验证） | 待验证 | 未修复 |
| B34 | P3 | bug | WeaselIPC/WeaselClientImpl.h:45 | session_id 跨线程非原子 | 待验证 | 未修复 |
| B35 | P3 | bug | 多处 | 杂项边界（见 B 路报告 P3 表） | 待验证 | 未修复 |
| B36 | P3 | perf | include/WeaselUtility.h:144-184 | escape/unescape 每串一个 stringstream（每键 ~6N 次） | — | 未修复 |

---

## 1. A 路审查详报

## Weasel 深度静态代码审查报告（A 路：WeaselTSF / WeaselServer / WeaselSetup / WeaselDeployer / test）

- 日期：2026-09-14
- 基线：当前 HEAD（acf9c6b），工作区无未提交改动
- 方法：对指定目录逐文件精读（WeaselTSF 除 ctffunc.h 外全部 .cpp/.h；WeaselServer、WeaselSetup、WeaselDeployer 全部 .cpp/.h；test/ 抽查），并对照旧报告 `weasel_CODE_REVIEW_20260906.md` 与其后约 25 个修复 commit，剔除已修复项、确认仍未修复项。
- 未审查（按任务要求）：include/wtl/、rime_api*.h、winsparkle*、librime/、deps/、plum/ 等；WeaselIPC/WeaselIPCServer/WeaselUI/RimeWithWeasel 不在本路范围，仅为判定本路问题而按需交叉阅读。

---

### 0. 总览

#### 新发现（不在旧报告中）

| 编号 | 严重度 | 类型 | 位置 | 一句话描述 |
|---|---|---|---|---|
| [A1] | P2 | bug | WeaselTSF/TextEditSink.cpp:35-44 | OnEndEdit 中 `tfSelection.range` 从不 Release，合成期间每次编辑会话（≈每击键）泄漏一个 ITfRange |
| [A2] | P2 | bug | WeaselTSF/CandidateList.cpp:230-234, 289-291 | `Destroy()` 销毁候选窗但不清 `_uiStarted`；`StartUI()` 因此早退不再重建窗口 —— 特定 abort 路径后本组合期内候选窗/预编辑永久不可见 |
| [A9] | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘快照在管道工作线程读 UI 的 style_/status_（含 std::wstring），与 UI 线程经 f8628c3 落地的写并发 —— 数据竞争，可致服务进程崩溃 |
| [A3] | P3 | bug | WeaselTSF/CandidateList.h:85, CandidateList.cpp:272-287 | `uiid` 未初始化即传入 `UpdateUIElement`（首个按键就会发生） |
| [A4] | P3 | bug | WeaselTSF/Compartment.cpp:75-89, Compartment.h:26 | `_Unadvise()` 对可能为 null 的 `_compartment` 解引用（Advise 半途失败后 Deactivate 即崩溃）；`_cookie` 未初始化 |
| [A5] | P3 | bug | WeaselTSF/DisplayAttribute.cpp:38-39 | `_SetCompositionDisplayAttributes` 在 range 为空时对可能为 null 的 `_pComposition` 解引用（当前为潜在路径） |
| [A6] | P3 | bug | WeaselTSF/WeaselTSF.h:239, WeaselTSF.cpp:152 | `_gaDisplayAttributeInput` 未初始化；`_InitDisplayAttributeGuidAtom()` 失败被忽略后把垃圾 guid atom 写进文档属性 |
| [A7] | P3 | bug/perf | WeaselTSF/LanguageBar.cpp:403-419 | 每键无条件读写 INPUTMODE_CONVERSION compartment；读取失败时 flags=0 回写会清掉无关的转换模式位 |
| [A8] | P3 | bug(死代码) | WeaselTSF/WeaselTSF.cpp:13-20 | `error_message`（打字线程弹模态框 + 非线程安全 static）已无任何调用者，成为陷阱型死代码 |
| [A10] | P3 | bug | WeaselServer/WeaselTrayIcon.cpp:22-38 | `Create` 把栈上 CIcon 的句柄存入 m_tnd.hIcon，函数返回后句柄悬垂，之后 AddIcon 复用死句柄 |
| [A11] | P3 | bug | WeaselServer/SystemTraySDK.cpp:427-439 | `SetIconList(HICON*, UINT)` 循环条件 `i <= nNumIcons` 差一越界（当前无调用者，潜在） |
| [A12] | P3 | bug | WeaselServer/SystemTraySDK.cpp:823-832, 694-697 | 双击分支 `!hSubMenu` 提前 return 泄漏 hMenu；`SetMenuDefaultItem` 对子菜单双重 DestroyMenu |
| [A13] | P3 | bug | WeaselSetup/WeaselSetup.cpp:94-111 | `/i` 流程中用户在选项对话框点"取消"仍继续安装（用默认 profile）；取消路径还沿用对话框打开前的 `_has_installed` 旧值 |
| [A14] | P3 | bug | WeaselSetup/WeaselSetup.cpp:68-76 | `CustomInstall` 读 RimeUserDir/Profile 的 `value[MAX_PATH]` 未强制 NUL 终止即构造 wstring（已知问题族的新位置） |
| [A15] | P3 | perf | WeaselTSF/EditSession.cpp:8-14 | 每击键堆分配 `shared_ptr<Context>` + `Config` + parser，可复用成员 |
| [A16] | P3 | bug | WeaselDeployer/UIStyleSettings.cpp:42-58, UIStyleSettingsDialog.cpp:66-79 | 配色预览路径用 `acptow` 解码 rime 的 UTF-8 路径（注释还自证"it is from ansi coding"）—— 非 ASCII 用户名下用户目录预览必失败 |

新发现合计 16 项：P1×0，P2×3，P3×13。

#### 旧报告已知且经本次读码确认仍未修复（重点摘录）

| 编号 | 严重度 | 位置（当前行号） | 描述 | 状态 |
|---|---|---|---|---|
| [K1]（旧 1.14） | **P1** | WeaselDeployer/Configurator.cpp:220-228 | `SyncUserData` 失败路径只补了 `CloseHandle(hMutex)`，仍不调 `EndMaintenance()` → 服务端永久停在维护模式，全系统禁输 | **仍未修复** |
| [K2]（旧 2.2/N14） | P2 | WeaselTSF/KeyEventSink.cpp:7-9, 37-60 | static 三件套跨实例/线程共享；`keyCountToSimulate!=0` 时 `*pfEaten` 不写即存入 static | 仍未修复 |
| [K3]（旧 2.3） | P2 | WeaselTSF/KeyEvent.cpp:44-51 | `ConvertKeyEvent` 函数级 static buf/table 非线程安全；`ToUnicodeEx` 传打包 KeyInfo 当扫描码 | 仍未修复 |
| [K4]（旧 2.5） | P2 | WeaselTSF/CandidateList.cpp:129 | `SysAllocStringLen(str.c_str(), size()+1)` BSTR 长度差一 | 仍未修复 |
| [K5]（旧 2.6 + 新补充） | P2 | WeaselTSF/Register.cpp:10, 226-231 | "Microsft" 拼写仍在；**且即使改对，`HKEY_CLASSES_ROOT` 下也看不到 `HKLM\SOFTWARE\Microsoft\CTF\TIP`（HKCR 只合并 Classes），该清理对真键永远无效** | 仍未修复 |
| [K6]（旧 2.22） | P2 | WeaselDeployer/SwitcherSettingsDialog.cpp:161,176,180 | `new const char*[]` 配标量 `delete`（UB） | 仍未修复 |
| [K7]（旧 2.23/N24） | P2 | SwitcherSettingsDialog.cpp:20-23；UIStyleSettings.cpp:5-8 | schema list 从不 `schema_list_destroy`；`custom_settings_init` 无对应 destroy | 仍未修复 |
| [K8]（旧 2.24） | P2 | SwitcherSettingsDialog.cpp:114-155 | 未初始化 `HKEY hKey`、无条件 `RegCloseKey`、`WaitForSingleObject(INFINITE)` 阻塞 UI 线程、value 未强制 NUL | 仍未修复 |
| [K9]（旧 2.25） | P2 | WeaselDeployer/DictManagementDialog.cpp:109-123 | 同步目录 CP_ACP 解码 UTF-8 + `dict_name[100]` 收 LB_GETTEXT 可溢出 | 仍未修复 |
| [K10]（旧 2.26） | P2 | DictManagementDialog.cpp:13,25 | STA 线程上无条件 `CoUninitialize` 拆主循环 COM 计数 | 仍未修复 |
| [K11]（旧 2.27） | P2 | WeaselSetup/imesetup.cpp:178-244, 387-464 | WOW64 重定向在 4 个提前 return 处不恢复；`install()` 不看 `install_ime_file` 结果继续写注册表 | 仍未修复 |
| [K12]（旧 2.28） | P2 | imesetup.cpp:364-375 | regsvr32 退出码不检查，注册失败仍报成功 | 仍未修复 |
| [K13]（旧 2.29） | P2 | imesetup.cpp:514-517 | 卸载不清 HKCU 配置；`RegDeleteKey` 有子键即失败 | 仍未修复 |
| [K14]（旧 N8） | P2 | WeaselSetup/WeaselSetup.cpp:109-111 | 已安装状态下改 profile 只写注册表、不重新注册 TSF profile | 仍未修复 |
| [K15]（旧 N9） | P2 | WeaselSetup/WeaselSetup.cpp:209-212 | `/userdir:"..."` 引号不剥离 | 仍未修复 |
| [K16]（旧 N10） | P2 | WeaselTSF/Composition.cpp:163,166-182 | GetTextExtent 会话泄漏 pRange 与 `selection.range`（每击键一次） | 仍未修复 |
| [K17]（旧 N11） | P2 | WeaselTSF/CandidateList.cpp:160-163 | `SetSelection` 不校验 nIndex（下游裸数组越界） | 仍未修复 |
| [K18]（旧 N22） | P2 | WeaselDeployer/Configurator.cpp:103-106 | `(… || configure_switcher(…)) && (… || configure_ui(…))` 短路：取消方案对话框会静默跳过 UI 风格设置 | 仍未修复 |
| [K19]（旧 N23） | P2 | Configurator.cpp:141-155 | `UpdateWorkspace` 不 `join_maintenance_thread` 即 EndMaintenance | 仍未修复 |
| [K20]（旧 2.31，更新） | P2 | test/TestWeaselIPC/TestWeaselIPC.cpp:143-146 | `AddSession(LPWSTR)` 与基类 `AddSession(LPWSTR, EatLine)`（include/WeaselIPC.h:64）参数不匹配仍不 override —— N5 修复时只改了 `ProcessKeyEvent` 签名，`AddSession` 漏改，该测试服务端的会话计数永远不增长 | 仍未修复 |
| [K21]（旧 N17） | P3 | WeaselTSF/WeaselTSF.cpp:177-190 | 每次线程焦点切换读注册表 + 2 次 IPC 往返（Echo + ProcessKeyEvent(0)） | 仍未修复 |
| [K22]（旧 P3 族） | P3 | 详见 §3 末尾汇总表 | （Compartment Advise 覆盖/重入、TextEditSink 清理跳过、E_FAIL/TRUE 返回值、dllmain 吞异常、WeaselService 死代码、user_name[20]、Deployer 静默退出、CustomInstall detached 线程、SetEnvironmentVariable throw 等） | 仍未修复 |
| [K23]（旧 4.性能 4/7 部分） | P3 | LanguageBar.cpp:198-220, 252-265, 403-419；Compartment.cpp:91-143 | 每键 compartment 读写、语言栏无条件 OnUpdate、GetIcon 每次读盘、`_IsKeyboardDisabled` 每键 ~8 次 COM | 仍未修复 |
| [K24]（旧 6.5-2） | P3 | WeaselTSF/KeyEventSink.cpp:65-74 | 失焦即 `_AbortComposition` 清空已输入编码，切回不恢复 | 仍未修复 |

---

### 1. 新发现详述

#### [A1]（P2, bug）OnEndEdit 泄漏 GetSelection 返回的 ITfRange —— 合成期间每击键一次

**位置**：`WeaselTSF/TextEditSink.cpp:35-44`

```cpp
if (pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1,
                           &tfSelection, &cFetched) == S_OK &&
    cFetched == 1) {
  ITfRange* pRangeComposition;
  if (_pComposition->GetRange(&pRangeComposition) == S_OK) {
    if (!IsRangeCovered(ecReadOnly, tfSelection.range, pRangeComposition))
      _EndComposition(pContext, true);
    pRangeComposition->Release();
  }
}   // ← tfSelection.range 从未 Release
```

- `ITfContext::GetSelection` 对每个返回的 `TF_SELECTION.range` AddRef，调用方必须 Release。同函数里 `pRangeComposition` 正确释放了，`tfSelection.range` 没有。
- **触发场景/影响**：`OnEndEdit` 在被 advise 的上下文上**任何**读写编辑会话结束后都会触发 —— 包括 WeaselTSF 自己的 start/insert/end 会话（这些会话都调用 `SetSelection`，`GetSelectionStatus(&fSelectionChanged)` 为真）。因此合成期间**每次击键都执行一次 GetSelection 并泄漏一个 ITfRange COM 对象**，长打字会话中宿主进程稳定增长（对象虽小但是热路径确定性泄漏）。多 UI 线程宿主按线程叠加。
- **修复**：`IsRangeCovered` 使用完后补 `tfSelection.range->Release();`。

#### [A2]（P2, bug）`Destroy()` 后 `StartUI()` 永不重建候选窗：本组合期内候选窗丢失

**位置**：`WeaselTSF/CandidateList.cpp:230-234`（Destroy）、`:289-291`（StartUI 早退）、触发链 `WeaselTSF/Composition.cpp:430-437`（`_AbortComposition`）+ `WeaselTSF/KeyEventSink.cpp:65-74` / `WeaselTSF.cpp:191-194`

```cpp
void CCandidateList::Destroy() {
  // EndUI();          ← 仍被注释（旧报告 2.4 的残余：修复只覆盖了 DestroyAll）
  Show(FALSE);
  _DisposeUIWindow(); // → _ui->Destroy() 销毁 panel 窗口
}
void CCandidateList::StartUI() {
  if (_uiStarted)     ← Destroy 未清 _uiStarted，此处早退
    return;
  ...                  // _MakeUIWindow()（窗口重建的唯一入口）永远走不到
}
```

- **机制**：`Destroy()` 不重置 `_uiStarted` 也不 `EndUI()`，窗口却已销毁；此后 `StartUI()` 因 `_uiStarted==true` 直接返回，`_MakeUIWindow()` 是窗口重建的唯一调用点（全仓库已核对）→ 窗口死到下一次 `EndUI()`（某次 commit/abort 走到 `_EndComposition(..., endUI=TRUE)`）把 `_uiStarted` 清零为止。
- **触发场景**：需要 `_pComposition == null` 而 `_uiStarted == true` 时发生 abort，具体路径：
  1. 不支持行内预编辑的宿主把空 TSF composition 终止（`OnCompositionTerminated` 中 `_status.composing==true` 分支只 `_FinalizeComposition()`，保留 UIElement —— 代码注释明确承认此类宿主存在），随后线程焦点切换 `OnKillThreadFocus`/`OnSetFocus(FALSE)` → `_AbortComposition` → `_IsComposing()` 为假 → 只走 `_cand->Destroy()`；
  2. `_StartComposition` 已 `StartUI()` 但异步 start 会话尚未执行/失败（窄窗口）时失焦；
  3. 宿主经 `ITfCandidateListUIElementBehavior::Abort()`（内部同样调 `_AbortComposition`）在 composition 已空时中止。
- **影响**：触发后**当前这轮 Rime 组合期间候选窗与预编辑完全不显示**（用户盲打），直到该组合以 commit/EndUI 结束、下一组合经 `StartUI` 重建窗口。不崩溃、输入仍能上屏。
- **待验证**：路径 1 的宿主出现频率（代码注释断言存在，未能运行时验证）；机制本身已逐行核实（含 `UIImpl::Show/Refresh` 对已销毁窗口均直接 return，见 WeaselUI/WeaselUI.cpp:51-79）。
- **修复**：恢复 `Destroy()` 中的 `EndUI()`（`EndUI` 已幂等：`if (!_uiStarted) return;`），或在 `StartUI` 早退分支检查 `!_ui->HasWindow()` 时补 `_MakeUIWindow()`。这同时收掉旧报告 2.4 的残余。

#### [A9]（P2, bug）托盘状态快照：管道线程无锁读 UI 的 style_/status_，与 UI 线程的写并发

**位置**：读侧 `WeaselServer/WeaselTrayIcon.cpp:40-53`（`RequestRefresh` → `WeaselTrayIconState::From(m_style, m_status)`），成员为绑定到 `weasel::UI` 内部成员的引用（`WeaselTrayIcon.h:79-80`）；写侧 `WeaselUI/WeaselPanel.cpp:1243-1252`（`OnApplyStyle`：UI 线程上 `m_style = *pStyle`，m_style 是对 `ui.style_` 的引用）与 `:1329-1346`（`_ApplyUpdate` 写 `ui.status_`）。

- f8628c3 把 ctx/status/style 的**写**全部 marshal 到 UI 线程（服务端主线程），修掉了旧 2.12 的写写/写读竞争主体；但托盘快照的**读**仍发生在管道工作线程：`_UpdateUI`（持 `g_api_mutex` 的 worker 线程）→ `_RefreshTrayIcon` → `_UpdateUICallback` → `RequestRefresh` → `From(m_style, m_status)`。
- UI 线程处理 `WM_WEASEL_SETSTYLE`/`WM_WEASEL_UPDATE` 时对同一批对象做整体赋值（`UIStyle` 拷贝赋值含 `std::wstring current_zhung_icon/current_ascii_icon` 等），worker 线程同时读取 → 对 `std::wstring` 的并发读写是 UB，撕裂读可致服务进程崩溃（全系统输入停止直至重启）。
- **触发场景**：方案切换（SetStyle 投递）期间另一应用的击键/焦点事件触发托盘刷新 —— 低频但真实存在，无法用锁以外的手段规避。
- **修复**（顺带是简化）：`ApplyRefresh()` 本来就运行在服务端消息线程（`SetTrayRefreshCallback`），把 `WeaselTrayIconState::From(m_style, m_status)` 从 `RequestRefresh` 挪到 `ApplyRefresh` 开头执行即可 —— 读和写同线程，竞态消失，`RequestRefresh` 退化为纯 PostMessage。

#### [A3]（P3, bug）`uiid` 未初始化即用于 `UpdateUIElement`

**位置**：`WeaselTSF/CandidateList.h:85`（`DWORD uiid;`，构造函数 `CandidateList.cpp:11-14` 未初始化）、使用点 `CandidateList.cpp:272-287`（`_UpdateUIElement` 无 `_uiStarted` 守卫）。

- `UpdateUI` 在**每个按键的响应处理**里都会调 `_UpdateUIElement()`（`WeaselTSF::DoEditSession` → `_UpdateUI` 无条件执行），而 `uiid` 只在 `StartUI()` 成功后才被 `BeginUIElement` 赋值。首个按键（尚无合成）即把**未初始化的栈/堆垃圾**传给 `ITfUIElementMgr::UpdateUIElement`。
- 影响：垃圾 id 通常不匹配任何已注册元素（msctf 返回错误，被忽略）；但元素 id 是从 1 开始的小整数，垃圾值恰好命中其他 TIP/外壳元素时会让无关元素被重绘。属读未初始化值的 UB。
- 修复：`DWORD uiid = 0;` + `_UpdateUIElement` 开头 `if (!_uiStarted) return S_OK;`。

#### [A4]（P3, bug）`CCompartmentEventSink::_Unadvise` 对空 `_compartment` 解引用；`_cookie` 未初始化

**位置**：`WeaselTSF/Compartment.cpp:75-89`（`_Unadvise` 第一行 `_compartment->QueryInterface(...)`），`:51-74`（`_Advise` 在 `GetCompartment`/QI 失败时不清场，`_compartment` 保持 null），`Compartment.h:26`（`DWORD _cookie;` 无初始化）。

- `_InitCompartment`（:189-205）在 `_Advise` **失败**时仍保留 sink 对象（首个 Advise 的失败甚至被第二个的结果覆盖，见 K22）；随后 `Deactivate()` → `_UninitCompartment()` → `_Unadvise()` → 对 null com_ptr 调 `QueryInterface` → **宿主应用内崩溃**。
- 修复：`_Unadvise` 开头 `if (_compartment == nullptr) { _cookie = 0; return S_FALSE; }`；`_cookie` 声明处初始化。

#### [A5]（P3, bug）`_SetCompositionDisplayAttributes` 对空 `_pComposition` 解引用（潜在）

**位置**：`WeaselTSF/DisplayAttribute.cpp:38-39`

```cpp
if (pRangeComposition == nullptr)
  hr = _pComposition->GetRange(&pRangeComposition);   // _pComposition 可能为 null
```

- 异步 `CInlinePreeditEditSession` 排队后、执行前 `_FinalizeComposition()` 已把成员置空的场景下，本分支即 null 解引用。当前唯一调用方（Composition.cpp:289）总是传非空 range，故为**潜在**缺陷；一旦有人新增调用点（传 nullptr 用默认 composition）即触发。
- 修复：`if (pRangeComposition == nullptr) { if (!_pComposition) return FALSE; ... }`。

#### [A6]（P3, bug）`_gaDisplayAttributeInput` 未初始化且初始化失败被忽略

**位置**：`WeaselTSF/WeaselTSF.h:239`（`TfGuidAtom _gaDisplayAttributeInput;`，构造函数未初始化）、`WeaselTSF.cpp:148-152`（`_InitDisplayAttributeGuidAtom();` 返回值被有意忽略，注释称部分应用会失败）、写入点 `DisplayAttribute.cpp:50-52`。

- `RegisterGUID` 失败的应用里（正是注释承认的那些 OpenGL 类应用），后续 `_SetCompositionDisplayAttributes` 把**未初始化的 atom** 写进 `GUID_PROP_ATTRIBUTE`；宿主读属性时得到非法 atom，轻则渲染异常，重则 msctf 内部断言。
- 修复：成员初始化为 0；`SetValue` 前 `if (_gaDisplayAttributeInput == 0) return FALSE;`。

#### [A7]（P3, bug + perf）`_UpdateLanguageBar` 每键无条件读写 compartment；失败路径清掉无关转换位

**位置**：`WeaselTSF/LanguageBar.cpp:403-419`，热路径调用点 `EditSession.cpp:16`（每击键）。

- `DWORD flags = 0; _GetCompartmentDWORD(flags, ...)` 读取失败（如 GetCompartment 失败/VT_EMPTY 之外的异常态）时 flags 保持 0，随后只按 ascii/full_shape 重建并回写 —— 会把宿主/用户设置的 `TF_CONVERSIONMODE_ROMAN/KATAKANA/NO_CONVERSION` 等位一并清零。正常路径（读到 VT_I4）无此问题。
- 同时这是旧报告性能项 4 的现状确认：**每键**一次 INPUTMODE_CONVERSION 读 + 一次写 + 无条件 `OnUpdate(TF_LBI_STATUS|TF_LBI_ICON)`（`UpdateWeaselStatus` :252-265 的 if 块为空操作，OnUpdate 恒调）——按"值有变化才写"可把稳态击键的这次 compartment 往返整个消掉。
- 修复：缓存上次写入值，无变化直接返回；读取失败时不回写。

#### [A8]（P3, 死代码）`error_message` 已无调用者

**位置**：`WeaselTSF/WeaselTSF.cpp:13-20`。全仓库无调用点（原调用者已随 1.10/1.6 修复移除）。函数体内仍是"输入线程弹模态框 + static GetTickCount 回绕判定反转"的实现，留着即是给未来的回归留坑。建议直接删除（旧 N16 随之关闭）。

#### [A10]（P3, bug）托盘图标句柄悬垂：`WeaselTrayIcon::Create` 传入栈上 CIcon

**位置**：`WeaselServer/WeaselTrayIcon.cpp:22-38`；受害方 `SystemTraySDK.cpp:199-204`（`m_tnd.hIcon = icon`）、`:295-307`（`AddIcon` 复用 `m_tnd.hIcon`）。

- `CIcon icon; icon.LoadIconW(IDI_ZH); CSystemTray::Create(..., icon, ...)` —— CIcon 到 HICON 的隐式转换把句柄存进 `m_tnd.hIcon`，`Create` 返回时 CIcon 析构 `DestroyIcon`。首次 NIM_ADD 系统已拷贝图标所以可见；但 explorer 重启（TaskbarCreated → `InstallIconPending` → NIM_ADD）或任何 RemoveIcon→AddIcon 周期都会用**已销毁的句柄**重新注册。后续 `Refresh` 的 SetIcon 会补救，但存在显示空图标/失败窗口。
- 修复：WeaselTrayIcon 持有一个拥有所有权的 CIcon 成员（或 AddIcon 前重载图标）。

#### [A11]（P3, bug）`CSystemTray::SetIconList(HICON*, UINT)` 差一越界

**位置**：`WeaselServer/SystemTraySDK.cpp:427-439`，`for (UINT i = 0; i <= nNumIcons; i++) m_IconList.push_back(pHIconList[i]);` —— 读取 `pHIconList[nNumIcons]` 越界一个元素。当前无调用者（仅 `SetIconList(UINT,UINT)` 同样未被使用），属启用即炸的潜在缺陷。修复：`< nNumIcons`。

#### [A12]（P3, bug）SystemTraySDK 菜单句柄泄漏/双重销毁

**位置**：`WeaselServer/SystemTraySDK.cpp:823-832`：双击分支 `if (!hSubMenu) return 0;` 未 `DestroyMenu(hMenu)`（对照单击分支 :782-784 有销毁）——泄漏一个菜单句柄；`:694-697`：`DestroyMenu(hSubMenu)` 后又 `DestroyMenu(hMenu)`（父菜单销毁会递归子菜单，构成对同一子菜单的二次销毁）。均为低频路径（默认项设置/双击），P3。

#### [A13]（P3, bug）`/i` 流程取消选项对话框不中止安装；`_has_installed` 过期

**位置**：`WeaselSetup/WeaselSetup.cpp:94-111`。

```cpp
if (IDOK != dlg.DoModal()) {
  if (!installing)
    return 1;  // aborted by user   ← 只有非 /i 流程才中止
}
...
if (!_has_installed)
  if (0 != install(profile, silent)) return 1;
```

- `/i`（安装脚本调用）模式下用户点"取消"仍会以默认 profile 继续完整安装（且 `silent` 可能为 false 弹出成功提示），取消语义失效。
- 取消路径不会执行 `_has_installed = dlg.installed;`（:106 只在 IDOK 分支），因此"先点移除再点取消"的 `/i` 会话会用**过期**的 `_has_installed=true`：跳过重装、照写配置、并触发 :137-152 的"修改成功"服务重启线程 —— 卸载后残留半配置状态。
- 修复：取消一律 `return 1`；或至少在取消分支同步 `dlg.installed`。

#### [A14]（P3, bug）`CustomInstall` 读注册表字符串未强制 NUL 终止

**位置**：`WeaselSetup/WeaselSetup.cpp:68-76`：`WCHAR value[MAX_PATH];` 未清零，`RegQueryValueEx` 成功且数据恰好填满缓冲时无 NUL，`user_dir = value;` / `profile = value` 越读。与旧报告 imesetup.cpp:482-486 同族的新位置。修复：改 `RegGetValueW`（自动终止）或 `value[_countof(value)-1]=0` + 按返回 len 截断。

#### [A15]（P3, perf）每击键的编辑会话堆分配 Context/Config

**位置**：`WeaselTSF/EditSession.cpp:8-14`：每个击键的 `DoEditSession` 里 `std::make_shared<weasel::Context>()` + `weasel::Config config` + `ResponseParser` 构造。Context 含多个 vector<Text>/wstring，是击键延迟路径上的纯开销（解析本身必须做，容器可以复用）。收益中等（每次一两次堆分配 + 解析器构造），实现简单：把 context/config 提为 WeaselTSF 成员，会话里 reset 复用。

#### [A16]（P3, bug）配色预览路径用 ACP 解码 UTF-8 —— 非 ASCII 用户名下预览失效

**位置**：`WeaselDeployer/UIStyleSettings.cpp:42-58`（`IfFileExist` 用 `acptow(filename)`，而 filename 由 rime 的 `get_user_data_dir()`（UTF-8）拼出）与 `WeaselDeployer/UIStyleSettingsDialog.cpp:73-75`：

```cpp
// it is from ansi coding, not utf8
image_.Load(acptow(file_path).c_str());
```

- 注释本身是错误断言：路径来源是 `rime_get_api()->get_user_data_dir()`，即 setup 时传入的 `wtou8()` 结果，**是 UTF-8**。中文用户名（`%APPDATA%` 含中文）下 `acptow` 产生乱码路径 → 用户目录的预览图永远找不到，回退共享目录（纯 ASCII 时碰巧可用）。与旧 2.25（DictManagementDialog 同类）同根。
- 修复：两处均改 `u8tow`，删除误导注释。

---

### 2. 旧报告已知且仍未修复 —— 头号项详述

#### [K1]（P1，旧报告 1.14）SyncUserData 失败路径仍把服务端留在维护模式

**位置**：`WeaselDeployer/Configurator.cpp:220-228`（当前 HEAD）

```cpp
{
  RimeApi* rime = rime_get_api();
  if (!rime->sync_user_data()) {
    LOG(ERROR) << "Error synching user data.";
    CloseHandle(hMutex);   // ← 修复只加了对 mutex 的释放
    return 1;              // ← 仍然跳过 EndMaintenance()
  }
  rime->join_maintenance_thread();
}
```

- 自旧报告提出以来此处只补了 `CloseHandle`；`client.EndMaintenance()` 仍在被跳过的路径之外。触发：用户"用户资料同步"时 rime 同步失败（用户目录只读/磁盘满/词典损坏均可）→ 已连上的 WeaselServer 进入维护态（托盘显示 Under maintenance、全部会话禁用）且**无人再叫醒它**，直到用户手动重启服务或恰好再跑一次部署。对一个输入法基础设施这是全系统级可用性故障。
- 修复：scope-exit 保证 `EndMaintenance()`（把 client 的 Start/End 配对封装成 RAII），失败时也恢复。

---

### 3. 旧报告已知且仍未修复 —— 其余确认清单（位置为当前行号）

P2 级（条目见 §0 表格 K2-K20，此处补关键行号细节）：

- K2：KeyEventSink.cpp:7-9（三 static）、:37-38（`if (!keyCountToSimulate)` 跳过赋值）、:54/:60（垃圾值入 static）。
- K3：KeyEvent.cpp:45-46（`static WCHAR buf[8]; static BYTE table[256];`）、:51（`ToUnicodeEx(vkey, UINT(kinfo), ...)`）。
- K4：CandidateList.cpp:129（`SysAllocStringLen(str.c_str(), (UINT)str.size() + 1)`）。
- K5：Register.cpp:10 拼写 + :226-231（HKCR 根键问题为本轮新补充：该清理逻辑对真实 TIP 键结构上无效，需改 HKLM 并核对删除结果）。
- K6：SwitcherSettingsDialog.cpp:161/176/180（`new[]`/`delete`）。
- K7：SwitcherSettingsDialog.cpp:20-23（两次 schema list 无 destroy）；UIStyleSettings.cpp:5-8（settings_ 无 destroy）。
- K8：SwitcherSettingsDialog.cpp:114（`HKEY hKey;` 未初始化）、:120-121、:127、:149、:155（无条件 close/INFINITE 等待/无 NUL）。
- K9：DictManagementDialog.cpp:110-112（`get_user_data_sync_dir` 结果按 CP_ACP 转）、:121-122（`dict_name[100]`）。
- K10：DictManagementDialog.cpp:13/:25（无条件 CoUninitialize）。
- K11：imesetup.cpp:178-244（WOW64 四处提前 return 不 Revert）；:387-464（`install()` 忽略 `install_ime_file` 结果继续写注册表/弹成功）。
- K12：imesetup.cpp:364-375（不看 regsvr32 退出码恒 return 0）。
- K13：imesetup.cpp:514-517（HKCU 不清理；RegDeleteKey 有子键即失败）。
- K14：WeaselSetup.cpp:109-111（修改安装不重新注册 profile）。
- K15：WeaselSetup.cpp:209-212（`/userdir:` 引号不剥离；另返回值直接把 LSTATUS 当退出码）。
- K16：Composition.cpp:163/176-182（GetTextExtent 会话 pRange 与 selection.range 双泄漏，每击键一次；[A1] 是另一调用点的同类问题）。
- K17：CandidateList.cpp:160-163（SetSelection 无上限校验，下游 GetCandidateRect 裸数组）。
- K18：Configurator.cpp:103-106（`&&` 短路跳过 configure_ui）。
- K19：Configurator.cpp:141-155（deploy 后不 join 即 EndMaintenance）。
- K20：test/TestWeaselIPC/TestWeaselIPC.cpp:143-146（`AddSession(LPWSTR)` 不 override 基类 `AddSession(LPWSTR, EatLine)`，include/WeaselIPC.h:64；无 `override` 关键字故编译不报。ProcessKeyEvent 的签名已在 N5 修复时同步，AddSession 漏网）。

P3 级（一行一条，均为代码核实仍未修复）：

| 位置 | 问题 |
|---|---|
| WeaselTSF/Composition.cpp:25,59 | StartComposition 会话成功路径仍返回 E_FAIL |
| WeaselTSF/EditSession.cpp:60 | `DoEditSession` 返回 TRUE(=S_FALSE) 而非 S_OK |
| WeaselTSF/Compartment.cpp:196-204 | 第一个 `_Advise` 的结果被第二个覆盖；存进 `DWORD hr` |
| WeaselTSF/Compartment.cpp:219-240 | OPENCLOSE 处理器内对自管 compartment `SetValue` 的重入隐患 |
| WeaselTSF/TextEditSink.cpp:78-86 | text-edit advise 失败时下轮清理整块跳过（layout sink 泄漏） |
| WeaselTSF/KeyEventSink.cpp:121-137 | `_fTestKeyDownPending` 在应用吞掉后续 OnKeyDown 时永久挂起 |
| WeaselTSF/KeyEvent.cpp:52-58 | `ToUnicodeEx` 返回 -1（死键）/2（代理对）未处理 |
| WeaselTSF/WeaselTSF.cpp:177-190 | 每次线程焦点切换读注册表 + Echo + ProcessKeyEvent(0)（旧 N17） |
| WeaselTSF/LanguageBar.cpp:122-135 | `GetActiveProfileLangId` 死代码 |
| WeaselTSF/dllmain.cpp:11-57,64 | 崩溃过滤器替换宿主过滤器且 `EXCEPTION_EXECUTE_HANDLER` 吞异常 |
| WeaselServer/WeaselService.cpp:70 | `boost::thread{...}` 临时对象析构即 terminate（死代码路径） |
| WeaselServer/WeaselService.cpp:153-155 | `Shutdown()` 报 STOPPED 不停 app；`_stoppedEvent` 创建即弃 |
| WeaselServer/WeaselServer.cpp:41-46 | `user_name[20]` 硬编码，超长用户名读取失败静默放行 |
| WeaselDeployer/WeaselDeployer.cpp:43-50 | 单实例互斥命中静默 `ret=1`，无提示 |
| WeaselSetup/WeaselSetup.cpp:137-152 | "修改成功" detached 线程 Sleep(500) 序列可被快速关框腰斩 |
| WeaselSetup/imesetup.cpp:342-344 | `SetEnvironmentVariable` 失败抛 `runtime_error` 无人接 → terminate |
| WeaselSetup/imesetup.cpp:36-58 | `.old.0~9` 占位副本重启后无人清理（累积最多 10 份） |
| WeaselSetup/imesetup.cpp:453-458 | WER DumpType=0 + CustomDumpFlags=0 几乎无 dump 信息（疑与意图不符） |
| WeaselDeployer/DictManagementDialog.cpp:61 等 | `find_module("levers")->get_api()` 不判空（3 处） |
| WeaselDeployer/DictManagementDialog.cpp:136-168 | restore 后 `user_dict_list_` 不刷新 |
| WeaselTSF/KeyEventSink.cpp:65-74 | 失焦 `_AbortComposition` 清空编码、切回不恢复（旧 6.5-2） |
| WeaselTSF/CandidateList.cpp:222-224 | `UpdateStyle` 无调用者（死代码）；style 变更仅在 StartUI 时落入 UI |
| test/TestWeaselIPC/TestWeaselIPC.cpp:36-39 | `/console` 分支 `return 0;` 不可达（微） |

性能类已知未修复（热路径，均经本轮确认仍在）：

- LanguageBar.cpp:403-419（每键 compartment 读+写）、:252-265（`UpdateWeaselStatus` 无条件 OnUpdate）、:198-220（自定义图标每次 `LR_LOADFROMFILE` 读盘）——建议按值缓存 + HICON 缓存。
- Compartment.cpp:91-143（`_IsKeyboardDisabled` 每键 ~8 次 COM 询问 + `GetKeyboardState`）——可用 compartment sink 缓存禁用态。
- WeaselTSF.cpp:177-190（每次窗口/线程焦点切换：注册表读 + Echo + ProcessKeyEvent(0) 两次 IPC 往返）。

范围外顺带确认（属其他审查路，仅备案）：RimeWithWeasel.cpp:73-87 的 explorer.exe detached-thread + Sleep(100) 特例（旧 2.13 残余）仍在；WeaselPanel 的 UI 线程 marshal（f8628c3/e7a62ef）主体正确。

---

### 4. 复核结论（避免后人重复排查）

以下点经逐行核对**无问题**，与旧报告附录结论一致或为其延伸：

- `Deactivate()` 的清理顺序与配对（ThreadFocusSink 已补 unadvise、DestroyAll 在 `_pThreadMgr` 置空前执行 EndUI）正确。
- `_EnsureServerConnected` 的 GetLastError 时序、`_reconnectRetry` 成员化、detached 线程不捕获 this —— 1.10/2.1 修复落地正确。
- `CInsertTextEditSession` 无 composition 时改走 `InsertTextAtSelection`（d40e968）落地正确；`CEndCompositionEditSession` 用会话自持 composition 并判空（e4c0d1d）落地正确。
- `GetCompartmentDWORD/SetCompartmentDWORD`（CompartmentUtil.cpp）指针生命周期与 HRESULT 语义正确（N2 修复有效）。
- FindIME（e54d49e）句柄配对正确。
- `_UpdateUIElement` 之外的 CCandidateList COM 引用计数（Attach 语义、EndUI/DestroyAll 幂等）自洽。
- WeaselTrayIcon 的 RequestRefresh/ApplyRefresh/DisableRefresh 三段式互斥与条件变量逻辑本身正确（[A9] 的问题仅在快照读取线程，不在该机制内）。
- PerUserReg/WeaselSetup 的 `/origsid` 机制（N6 修复）实现正确。

---

## 2. B 路审查详报

## Weasel 深度静态代码审查报告（B 路：IPC / 服务端 / RimeWithWeasel / WeaselUI / 共享头文件）

- **日期**：2026-09-14
- **基线**：HEAD = `acf9c6b`（工作区）
- **范围**：`WeaselIPC/`、`WeaselIPCServer/`、`RimeWithWeasel/`、`WeaselUI/` 及 `include/` 自研共享头（PipeChannel.h、PipeServer.h、ResponseParser.h、WeaselIPCData.h、WeaselUtility.h、StringAlgorithm.hpp、KeyEvent.h、WeaselConstants.h、logging.h、VersionHelpers.hpp、WeaselIPC.h、WeaselUI.h、RimeWithWeasel.h），约 1 万行逐文件精读。
- **方法**：纯静态审查；已先读 `weasel_CODE_REVIEW_20260906.md` 全文与 `git log --oneline -40`，对照确认约 25 个修复 commit，**不重复已修复项**；旧报告列出且经本次逐行复核确认仍存在的，标注"旧报告已知且仍未修复"。为验证疑点，额外对照了上游 rime/weasel 的 0.15.0 / 0.16.2 / master 三版源码。
- 不含：WeaselTSF/、WeaselDeployer/、WeaselSetup/、update/（其他路范围）；第三方代码。

**严重度统计**：P1 × 1，P2 × 13（bug 9 + perf 4），P3 × 22（bug 21 + perf 1）。共 36 项。

---

### 0. 总览表

| 编号 | 严重度 | 类型 | 位置 | 一句话描述 |
|---|---|---|---|---|
| B1 | P1 | bug | WeaselUI/WeaselUI.cpp:50-94 等 | UI 的 Show/Hide/ShowWithTimeout 未走 marshal，管道线程持 g_api_mutex 跨线程 ShowWindow，与消息线程的 g_api_mutex 段构成死锁环 |
| B2 | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘刷新在管道线程读 ui.style_/status_（wstring），与 UI 线程写入构成数据竞争（旧 2.13 残留，方向反转） |
| B3 | P2 | bug | WeaselUI/StandardLayout.cpp:98 | `substr(start, end)` 第二参误当长度，高亮段测量过长（旧 1.13 已知未修复，WeaselPanel 处已修此处未修） |
| B4 | P2 | bug | WeaselIPC/ContextUpdater.cpp:55-62 | 守卫 `vec.size() < 2` 却读 `vec[2]`，越界读（旧 2.10 已知未修复） |
| B5 | P2 | bug | WeaselIPC/Deserializer.h:8-16 | 反序列化异常在打字应用输入线程弹 MessageBoxA 模态框（旧 2.10 已知未修复） |
| B6 | P2 | bug | include/PipeChannel.h:64-67 | TSS 管道句柄线程退出只 delete 指针不 CloseHandle（旧 2.7 已知未修复） |
| B7 | P2 | bug | WeaselUI/DirectWriteResources.cpp:103-106 | font_face 为空串时 `ws_split(...)[0]` 越界（MSVC 空串产出空 vector）（旧 N13 已知未修复） |
| B8 | P2 | bug | include/WeaselUtility.h:315-321 | HR() 对 S_FALSE 也抛 ComException，UI 绘制路径无局部 catch，异常穿 WNDPROC 直达 server main → 整个输入法服务退出（旧 2.19 已知未修复） |
| B9 | P2 | bug | WeaselUI/VerticalLayout.cpp:215 等 | `cinfo.highlighted` 无上限校验直接索引 `_candidateRects[id]` 裸数组（旧 N11 已知未修复） |
| B10 | P2 | bug | WeaselUI/WeaselPanel.h:158-162 | m_istorepos/m_offsetys/m_offsety_* 未初始化即读（旧 2.17 已知未修复） |
| B11 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp | 每键 ~7 次 rime 交叉：直通键无早退（旧 4.3 已知未修复）+ get_status 每键两次 + get_property("client_app") 每键一次（新） |
| B12 | P2 | perf | WeaselUI/WeaselPanel.cpp、StandardLayout.cpp | 每键整窗重算重绘：布局对象全重建、每串建 2 个 IDWriteTextLayout、绘制再建、每个高亮整幅 GDI+ 位图 + 4 趟盒模糊（旧 4.5 已知未修复） |
| B13 | P2 | perf | include/WeaselIPCData.h:103,166 | notequal/operator== 按值深拷贝候选向量（旧 4.5c/6.4.3 已知未修复） |
| B14 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp:73-87 | explorer.exe 每键 detached 线程 + Sleep(100) 垫片（旧 2.13 已知未修复） |
| B15 | P3 | bug | WeaselIPC/WeaselClientImpl.cpp:145-152,185-191 | StartSession 失败后 channel body 残留，下次 START_SESSION 拼出双份客户端信息 |
| B16 | P3 | bug | include/PipeChannel.h:171-184 | body 超过 64KB 缓冲时 stream failbit → tellp()==-1 → 静默只发头不发 body（旧 2.11 变体，仍未修复） |
| B17 | P3 | bug | WeaselIPCServer/WeaselServerImpl.cpp:445-461 | Listen() 的 catch(...) 后无退避立即重试，CreateNamedPipe 持续失败时变成 100% CPU 忙循环 |
| B18 | P3 | bug | WeaselIPCServer/WeaselServerImpl.cpp:450-455,496-519 | worker 先于 _RegisterWorker 结束时，_RemoveWorker 竞态导致 m_workers 残留已关闭句柄 |
| B19 | P3 | bug | WeaselUI/WeaselPanel.cpp:1261-1264 | MoveTo 的 marshal 不检查 PostMessage 返回值，投递失败泄漏 RECT（与 ApplyUpdate/ApplyStyle 不一致） |
| B20 | P3 | bug | WeaselIPC/Configurator.cpp:17-21 | 守卫检查 p_context 却解引用 p_config（复制粘贴错，当前生产路径不可达 NULL，属埋雷） |
| B21 | P3 | bug | WeaselIPC/Deserializer.cpp:13-28 | s_factories 无锁懒初始化，多线程首次解析并发 Define 数据竞争 |
| B22 | P3 | bug | RimeWithWeasel/RimeWithWeasel.cpp:549 等 | get_session_status(0)/未知 ipc_id 经 operator[] 向会话表插入死条目（旧 2.9 同类，已知未修复） |
| B23 | P3 | bug | WeaselUI/StandardLayout.cpp:6-12、RimeWithWeasel.cpp:871-873 | swprintf_s 遇超长 label / 配置串异常格式符 → invalid parameter 直接终止进程（旧 P3 已知未修复） |
| B24 | P3 | bug | WeaselUI/WeaselPanel.cpp:1003 | DoPaint 每帧 ModifyStyleEx（旧 2.20 已知未修复） |
| B25 | P3 | bug | WeaselUI/WeaselPanel.cpp:1088-1091 | EndDraw 失败仍把无文字帧送上分层窗口（旧 2.21 已知未修复） |
| B26 | P3 | bug | WeaselUI/DirectWriteResources.cpp:98-136 | init_font 忽略 wrap 形参，preedit 换行配置失效（旧 2.18 已知未修复） |
| B27 | P3 | bug | WeaselUI/FullScreenLayout.cpp:68-123 | AdjustFontPoint 永久污染共享 pDWR 的字号（旧 N12 已知未修复） |
| B28 | P3 | bug | WeaselIPCServer/WeaselServerImpl.cpp:307-315 | PhysicalToLogicalPointForPerMonitorDPI 每键 GetProcAddress 且不判空（旧 P3 已知未修复） |
| B29 | P3 | bug | RimeWithWeasel/RimeWithWeasel.cpp:27-31 | _GenerateNewWeaselSessionId 按值拷贝整个会话表（旧 6.4.3 已知未修复） |
| B30 | P3 | bug | RimeWithWeasel/RimeWithWeasel.cpp:1462-1463 | _GetStatus 对 schema_name/schema_id 未判空，NULL 构造 std::string UB（旧 N20 已知未修复） |
| B31 | P3 | bug | include/WeaselUtility.h:14-32 | getUsername 第二次调用失败未校验，用未初始化缓冲构造 wstring |
| B32 | P3 | bug | RimeWithWeasel/RimeWithWeasel.cpp:177 | rime_api->create_session() 返回 0 未检查，后续全链路静默失败 |
| B33 | P3 | bug | RimeWithWeasel/RimeWithWeasel.cpp:394-417,689 | OnNotify 与 _ShowMessage/_UpdateUI 共用非递归互斥，get_state_label 若同步发通知则自锁（旧 N19，待验证） |
| B34 | P3 | bug | WeaselIPC/WeaselClientImpl.h:45 | session_id 跨线程非原子读写（多 UI 线程宿主形式 UB，x86 实际良性） |
| B35 | P3 | bug | 多处 | 杂项边界（详见 P3 杂项节） |
| B36 | P3 | perf | include/WeaselUtility.h:144-184 | escape/unescape 每串构造一个 stringstream，每键 ~3N+2 次 |

---

### 1. P1

#### [B1] UI 的 Show/Hide/ShowWithTimeout 未纳入 marshal 体系：管道线程持 g_api_mutex 跨线程 ShowWindow，可与服务消息线程互等死锁 — bug

**位置**
- 未 marshal 的跨线程窗口操作：`WeaselUI/WeaselUI.cpp:50-59`（`Show` → `panel.ShowWindow(SW_SHOWNA)`）、`:61-70`（`Hide`）、`:72-81`（`ShowWithTimeout` → `ShowWindow` + `SetTimer`）、`:25-32`（`Update` 先 `Hide` + `KillTimer`）
- 管道线程调用点（均在 `g_api_mutex` 内执行）：`RimeWithWeasel/RimeWithWeasel.cpp:223`（RemoveSession）、`:369`（FocusOut）、`:556-557`（_UpdateUI，**每次按键**都会走到）、`:742`（_ShowMessage → ShowWithTimeout）、`:1485`（_GetStatus → ShowWithTimeout）
- 消息线程持 `g_api_mutex` 的窗口消息处理器：`WeaselIPCServer/WeaselServerImpl.cpp:47`（OnColorChange，WM_SETTINGCHANGE/WM_DWMCOLORIZATIONCOLORCHANGED）、`:91`（OnEndSystemSession，注销/关机）、`:106`（OnCommand，托盘菜单）

**问题**：e7a62ef 与 f8628c3 把 `Refresh/RedrawWindow/MoveTo/ApplyUpdate/ApplyStyle` 全部 marshal 回 UI 线程（`WeaselPanel.cpp:145/1137/1261/1316/1351`），但 `UIImpl::Show/Hide/ShowWithTimeout` 仍由 IPC 工作线程**直接**调用窗口 API。`ShowWindow`/`SetWindowPos` 对他线程窗口会发起**同步** send（WM_SHOWWINDOW/WM_WINDOWPOSCHANGING 等），需等窗口所属线程泵消息才能返回；而服务端消息线程在 OnColorChange/OnCommand/OnEndSystemSession 内**正阻塞在获取 g_api_mutex** 时不会泵消息。

**触发场景/影响**：
```
管道线程: lock(g_api_mutex) ──> m_ui->Hide()/ShowWithTimeout() ──> ShowWindow 同步 send，等 UI 线程泵消息
消息线程: OnCommand/OnColorChange ──> 等 g_api_mutex（被管道线程持有）  ──> 永不泵消息
```
两边互等，全进程输入冻结（所有应用打不了字），托盘无响应，只能杀进程（随后 `RegisterApplicationRestart` 又拉起，掩盖问题）。典型触发组合：提示窗正在显示/隐藏切换（ascii 切换、方案切换的 1.2s 倒计时窗）时用户点击托盘菜单，或主题切换（WM_SETTINGCHANGE 广播相当常见）/注销与击键并发。概率低但后果是全局输入中断，且 `_UpdateUI` 每键都调用 `m_ui->Hide()` 使暴露面持续存在。

**修复建议**：把 Show/Hide/ShowWithTimeout 纳入同一 marshal 通道（如 `PostMessage(WM_WEASEL_SHOW/HIDE/SHOW_TIMEOUT)`，超时值放 wParam 或堆分配），保证"窗口 API 只在窗口线程调用"成为不变量；顺带 `UIImpl::Update` 开头的 `Hide()+KillTimer` 同样迁移。

---

### 2. P2（bug）

#### [B2] 托盘状态快照在管道线程读 UI 引用成员，与 UI 线程写入构成数据竞争 — bug（旧报告 2.13 残留，方向已反转）

**位置**：`WeaselServer/WeaselTrayIcon.cpp:13-14`（`m_style(ui.style())`、`m_status(ui.status())` 为绑定到 `UI` 成员的引用）、`:45`（`m_pending_state = WeaselTrayIconState::From(m_style, m_status)`）；写侧：`WeaselUI/WeaselPanel.cpp:1251`（`m_style = *pStyle`，UI 线程 OnApplyStyle）、`:1334`（`m_status = status`，UI 线程 OnApplyUpdate）。

**问题**：`RequestRefresh` 由 `_UpdateUICallback` 在**管道工作线程**执行（`RimeWithWeasel.cpp:560`，经 `_RefreshTrayIcon`；explorer 分支更是延迟 100ms 的 detached 线程，窗口更宽）。`WeaselTrayIconState::From` 拷贝 `style.current_zhung_icon` / `current_ascii_icon` 等 `std::wstring`，而 UI 线程此刻可能正在 `OnApplyStyle/OnApplyUpdate` 里对同一对象做赋值——`m_state_mutex` 只保护托盘自己的 pending 状态，不覆盖这次读。非原子并发读写 `std::wstring` 是 UB，撕裂读可导致服务进程崩溃。

**触发场景**：切换方案/深色模式（style 投递到 UI 线程）与击键触发的托盘刷新并发；explorer.exe 下每键一线程使窗口扩大。

**修复建议**：让 `RequestRefresh` 不再接收 UI 引用——由调用方（管道线程，已持 g_api_mutex 且序列化于 handler 状态）从 `session_status.style`/`weasel_status` 本地副本生成 `WeaselTrayIconState` 作为参数传入；或在 `OnApplyStyle/OnApplyUpdate` 落地后由 UI 线程自行投递快照。同时删除 explorer.exe 的 100ms detached 线程垫片（见 B14）。

#### [B3] StandardLayout::GetPreeditSize 高亮子串 `substr(start, end)` 长度参数误用 — bug（旧报告 1.13/V1 已知且仍未修复）

**位置**：`WeaselUI/StandardLayout.cpp:98`

```cpp
std::wstring hilited_str = preedit.substr(_range.start, _range.end);  // 第二参是 count 不是 end
```

`WeaselPanel.cpp:683` 的同款代码已在早前修复为 `substr(range.start, (size_t)range.end - range.start)`，但布局侧这一处漏修。`_range.start > 0`（光标移入合成串中部）时 `hilited_str` 吞掉尾部文本 → `_hilitedsz` 测量过长 → 布局在 before/hilited/after 之间出现空隙、候选窗整体偏大。已在本仓旧报告 V1 用独立程序复现过（`substr(3,5)` 得 5 字符）。

**修复建议**：改为 `preedit.substr(_range.start, _range.end - _range.start)`。

#### [B4] ContextUpdater 光标解析守卫不足读 `vec[2]` — bug（旧报告 2.10/V2 已知且仍未修复）

**位置**：`WeaselIPC/ContextUpdater.cpp:55-62`

```cpp
if (vec.size() < 2)
  return;
...
attr.range.cursor = _wtoi(vec[2].c_str());  // size()==2 时越界
```

服务端目前总发 3 个字段（`RimeWithWeasel.cpp:846-853`），但这是纯约定无校验：任何一次只带 `start,end` 的响应（协议演进/截断）都会越界——MSVC debug 触发 `vector subscript out of range` 断言（旧报告 V2 已复现），release 读垃圾值直接污染 `TextRange.cursor`，影响高亮与布局。

**修复建议**：`if (vec.size() != 3) return;` 或对 cursor 单独判 `vec.size() >= 3`。

#### [B5] IPC 反序列化异常在打字应用输入线程弹模态 MessageBox — bug（旧报告 2.10 已知且仍未修复）

**位置**：`WeaselIPC/Deserializer.h:8-16`

```cpp
} catch (const boost::archive::archive_exception& e) {
  const std::string msg = ...;
  MessageBoxA(NULL, msg.c_str(), "IPC exception", MB_OK | MB_ICONERROR);
```

`ContextUpdater::_StoreCand`/`Styler::Store` 运行在宿主应用的输入线程；归档数据异常（如 B16 的截断 body、版本不匹配）即弹模态框，该线程全部输入被冻结直到用户点掉。另外 `ContextUpdater.cpp:73-76` 的 `std::wstringstream ss(value); text_wiarchive ia(ss);` 仍在 `TryDeserialize` 的 try 之外（旧报告同条附带项），构造期抛出会直接冒泡。

**修复建议**：改 `LOG(ERROR)`/`DLOG`；把归档构造一并挪进 try；顺带对 `candies.size()` 加上限校验。

#### [B6] 每线程管道句柄 TSS 无清理函子：线程退出泄漏内核句柄并钉死服务端 worker — bug（旧报告 2.7 已知且仍未修复）

**位置**：`include/PipeChannel.h:64-67`

```cpp
mutable boost::thread_specific_ptr<HANDLE> hpipe_ptr;   // 无 cleanup functor
...
hpipe_ptr.reset(new HANDLE(INVALID_HANDLE_VALUE));      // 线程退出时仅 delete 指针
```

boost TSS 缺省清理是 `delete`，`CloseHandle` 永不执行。客户端侧（WeaselTSF.dll 在宿主进程内）每个用过输入法后退出的线程泄漏一个管道句柄；服务端为该连接保留一个管道实例 + 一个 `_ProcessPipeThread` 工作线程阻塞在 `ReadFile`，直到**宿主进程**退出才释放。多线程 UI 频繁建线程的应用（浏览器渲染进程、IDE）会持续放大服务端线程数。

**修复建议**：TSS 存 RAII 包装（析构 `_FinalizePipe`），即 `thread_specific_ptr<PipeHandleOwner>`。

#### [B7] font_face 为空串时 `ws_split(...)[0]` 越界 — bug（旧报告 N13 已知且仍未修复）

**位置**：`WeaselUI/DirectWriteResources.cpp:103-106`

```cpp
fontFaceStrVector = ws_split(fontface, L",");
...
fontFaceStrVector[0] = std::regex_replace(fontFaceStrVector[0], ...);
```

MSVC 的 `wsregex_token_iterator` 对空串产出**空 vector**（libstdc++ 才给一个空 token），`font_face` 为空时 `[0]` 越界。`_UpdateUIStyle` 只在 `font_point<=0` 时兜底 12，`font_face` 没有非空兜底（`UIStyle` 默认 `font_face()` 为空串）：weasel.yaml/方案缺失 `style/font_face` 即可让服务端或宿主崩溃。`SetDpi`/`AdjustFontPoint`（B27）重建资源时同样走此路径。

**修复建议**：`if (fontFaceStrVector.empty()) fontFaceStrVector.push_back(L"");` 或在 `_UpdateUIStyle` 给 font_face 一个非空默认值。

#### [B8] HR() 对 S_FALSE 也抛异常，且 UI 绘制路径无局部 catch：一次 DWrite 瞬时失败 = 服务进程退出 — bug（旧报告 2.19 已知且仍未修复）

**位置**：`include/WeaselUtility.h:315-321`（`if (S_OK != result) throw`）；异常逃逸面：`WeaselPanel.cpp` 全部 `HR()`（`_TextOut:1446-1465`、`DoPaint` 间接路径）、`StandardLayout.cpp:30-71`（GetTextSizeDW 每串 5 处 HR）、`DirectWriteResources.cpp:36-51,110-131,235-276`；唯一进程级 catch 在 `WeaselServer/WeaselServer.cpp:117-122`。

e7a62ef 修复的是"跨线程并发访问 render target"导致的 0xC0000005，异常路径未动：字体串损坏、`GetSystemFontFallback`/`CreateTextLayout` 返回失败（含合法的 `S_FALSE`）都会经 WNDPROC 一路抛到 server main 的 `catch(...)` → **WeaselServer 退出**，全系统输入中断到被重启为止（会话全丢）。`GetLayoutOverhangMetrics`（WeaselUI.h:129-131）还会在 `pTextLayout` 为空时先于检查解引用。

**修复建议**：`HR` 改为 `FAILED(hr)` 才抛；`DoPaint/DoLayout` 外层包 try/catch，失败帧跳过绘制并计数降级（重建资源后下一帧恢复）。

#### [B9] `cinfo.highlighted` 无上限校验直接索引 100 元素裸数组 — bug（旧报告 N11 已知且仍未修复）

**位置**：`WeaselUI/VerticalLayout.cpp:215`、`HorizontalLayout.cpp:221`、`VHorizontalLayout.cpp:229/451/497/513`（`_highlightRect = _candidateRects[id];`）；消费侧 `WeaselPanel.cpp:328/363/875/877`（`GetCandidateRect(m_ctx.cinfo.highlighted)`、`m_offsetys[highlighted]`）；数组定义 `StandardLayout.h:75-82`（容量 `MAX_CANDIDATES_COUNT=100`）。

`id` 绑定 `cinfo.highlighted`（`Layout.h:15/120`），全链路无一处校验。应用进程内的候选窗数据来自 IPC 反序列化（`Styler/ContextUpdater` 无校验），服务端异常值或恶意管道数据 → OOB 读（`highlighted` 任意大时是真越界）→ 垃圾矩形/崩溃于宿主进程。

**修复建议**：`WeaselPanel::Refresh` 计算 `m_candidateCount` 处一并 clamp：`if (highlighted < 0 || highlighted >= m_candidateCount) highlighted = 0;`（布局构造函数或 Layout 基类统一 clamp 更彻底）。

#### [B10] m_istorepos / m_offsetys / m_offsety_* 未初始化即读 — bug（旧报告 2.17 已知且仍未修复）

**位置**：声明 `WeaselUI/WeaselPanel.h:158-162`；构造函数 `WeaselPanel.cpp:60-100` 未初始化这批成员；首读 `DoPaint:1015`（`if (m_istorepos)`）及鼠标命中测试 `:329/364/438/508` 等。`_RepositionWindow` 仅在 `adj==true`（`:1403`）或翻转到输入框上方（`:1417`）时才写入 `m_istorepos`。

首次绘制/点击发生在窗口翻转之前时读的是未初始化内存：`m_istorepos` 为垃圾真值时用同样未初始化的 `m_offsetys[i]` 做偏移 → 绘制/命中测试位置错乱；`GetIsReposition()`（WeaselPanel.h:100）还把该值透传给 TSF 的按键上下翻转判断（KeyEventSink）。

**修复建议**：成员就地初始化 `bool m_istorepos = false; int m_offsety_preedit = 0; ...`，`m_offsetys` 构造时 `std::fill`。

---

### 3. P2（性能）

热路径定义：每次击键（含纯 ASCII 直通键）都会执行 `ProcessKeyEvent → _Respond → _UpdateUI`（服务端，全程持 `g_api_mutex`）+ TSF 侧解析 + `ApplyUpdate → Refresh → DoLayout → DoPaint`（应用进程 UI 线程）。

#### [B11] 击键路径 rime C 交叉 ~7 次：直通键无早退、get_status 每键两次、get_property("client_app") 每键一次 — perf（部分为旧报告 4.3 已知未修复，部分新增）

**位置**：`RimeWithWeasel/RimeWithWeasel.cpp:270-298`（ProcessKeyEvent）、`:756-953`（_Respond）、`:532-569`（_UpdateUI）、`:1452-1491`（_GetStatus）、`:73-87`（_RefreshTrayIcon）。

实测计数（组合中会话、单按键）：
1. `process_key`（:278）
2. `get_commit`（:765）
3. `get_status`（:775，_Respond）
4. `get_context`（:806，_Respond；直通键也调用，只是无候选）
5. `get_status` **第二次**（:1458，_UpdateUI→_GetStatus）——与 #3 完全重复，键间隔内状态几乎不可能变化
6. `get_option("inline_preedit")`（:550）
7. `get_property("client_app")`（:77，仅为判断是否 explorer.exe 决定托盘刷新方式）——`client_app` 是会话属性、AddSession 后不变，却在**每键**查询

对未吃键（英文直通）同样全跑一遍。旧报告 4.3（直通键早退）未修复；#5、#7 是本次新增计数。

**优化思路**：(a) `!handled && 无 commit && !is_composing` 时跳过 get_context/UI 全刷新（旧 4.3，估服务端空闲键 -50% 工作量）；(b) `_Respond` 已取的 `RimeStatus` 传给 `_UpdateUI` 复用，省一次全量结构拷贝 + C 交叉；(c) `client_app` 在 `_ReadClientInfo` 已拿到，缓存到 `SessionStatus`，`_RefreshTrayIcon` 改查缓存（省每键 1 次跨 DLL 调用与字符串拷贝）。

#### [B12] 候选窗每键"整窗重算 + 整窗重绘"：布局全重建、每串 2 个 IDWriteTextLayout、绘制期再建、每高亮整幅 GDI+ 位图 + 4 趟盒模糊 — perf（旧报告 4.5a/4.5b 已知未修复，本次给出当前代码下的量化）

**位置与计数**（9 候选 @150% DPI 的一次典型刷新）：
- `WeaselPanel::Refresh:181-182` 每次 `_CreateLayout()`（new + delete 布局对象）+ `DoLayout`；
- `StandardLayout::GetTextSizeDW:30-64` 每个被测字符串建 **2** 个 `IDWriteTextLayout`（第二个只为拿 overhang）；`VerticalLayout::DoLayout` 对每候选测 label/text/comment ≈ 30+ 个 layout/帧（旧 4.5b）；
- `WeaselPanel::_TextOut:1450` 绘制期对**每段文本再建**一个 layout（测量结果未复用）；
- `_HighlightText:582-609` 每个高亮背景分配整幅 `Gdiplus::Bitmap`（rc + 2×blurMargin）并跑 `DoGaussianBlur`（`GdiplusBlur.cpp:287-305`，3 次 `boxBlur_4` 共 12 趟 O(w·h) 扫描）——9 候选 + preedit 高亮 + 背景阴影 ≈ 每帧 11 次全幅模糊（旧 4.5a）。

**优化思路**：(a) 按 `(尺寸, 半径, 颜色)` 缓存模糊位图（候选内容变化时矩形尺寸高度复用），估阴影主题绘制提速 5-20×；(b) `GetTextSizeDW` 第二个 layout 仅在配置了 max_width/max_height 换行时创建，其余用 `SetWordWrapping(NO_WRAP)` 的单一 layout 取 metrics+overhang；(c) 测量出的 `DWRITE_TEXT_METRICS` 缓存在布局矩形旁，绘制期 `_TextOut` 直接复用（省每串一次 layout 创建 + 一次 DWrite 排版）。这些都在每键执行的应用 UI 线程上，直接决定跟手程度。

#### [B13] 去重比较按值深拷贝候选向量 — perf（旧报告 4.5c/6.4.3 已知未修复）

**位置**：`include/WeaselIPCData.h:103`（`notequal(std::vector<Text> txtSrc, std::vector<Text> txtDst)` 按值）、`:166`（`Status::operator==(const Status status)` 按值）。

`_ApplyUpdate` 的 `m_ctx == ctx` 去重（WeaselPanel.cpp:1331）每键触发：`CandidateInfo::operator==` 内 3 次 `notequal` = **6 份 vector<Text> 深拷贝**（每份 N 个 wstring），比较完即扔。内容不变时（翻页高亮移动之外的大量场景）纯属浪费。

**优化思路**：改 `const std::vector<Text>&`；收益：每键省 6 次向量深拷贝与堆分配，去重命中时尤为明显。改动零风险。

#### [B14] explorer.exe 特例每键起一个 detached 线程 + Sleep(100) — perf（旧报告 2.13 已知未修复）

**位置**：`RimeWithWeasel/RimeWithWeasel.cpp:79-84`

```cpp
if (!ret || u8tow(app_name) == std::wstring(L"explorer.exe"))
  boost::thread th([=]() { ::Sleep(100); if (_UpdateUICallback) _UpdateUICallback(); });
```

在资源管理器地址栏/文件对话框/开始菜单输入时**每键**创建并丢弃一个线程（还有 B2 的数据竞争放大）。d73f629 的 PostMessage 合并刷新落地后该垫片已无必要；`get_property` 失败（`!ret`）时也走此分支，误判面更大（顺带：`static char app_name[256]` 跨会话共享残留，旧 N21）。

**优化思路**：删除线程，直接 `_UpdateUICallback()`（即 `RequestRefresh`，内部已有 pending 合并）；`!ret` 时用缓存的 app_name（见 B11c）。

---

### 4. P3

| 编号 | 位置 | 描述 | 备注 |
|---|---|---|---|
| B15 | WeaselClientImpl.cpp:145-152, 185-191 | StartSession 的 Transact 在 `_Ensure` 即抛出时 `_Send` 未执行、`ClearBufferStream` 不运行，body/write_stream 残留；下次 `_WriteClientInfo` 在旧流上追加 → START_SESSION body 出现两份 `action=session...`，或残留 body 搭车下一个无 body 命令。服务端解析到首个 `.` 即止，功能侥幸不受影响 | 新发现 |
| B16 | PipeChannel.h:171-184 | body 超过 64KB 时 wbufferstream 置 failbit、`tellp()` 返回 -1 → `body_bytes=0` → 只发头不发 body（旧 2.11 钳位截断的变体）：超长 PREVIEW_ALL/注释时 START_SESSION 静默丢 client_app、cinfo 丢整段。建议对 `has_body && body_bytes==0` 记日志并 fail-fast | 旧 2.11 变体 |
| B17 | WeaselServerImpl.cpp:445-461 | `Listen()` 的 `catch(...) { _FinalizePipe; }` 后立即回到 `ConnectNamedPipe`：`CreateNamedPipe` 持续失败（句柄耗尽等）→ 无 sleep 的紧密循环烧满一核。加 50-100ms 退避即可 | 新发现（ec079ba 把 catch(DWORD) 放宽为 catch(...) 时引入的回归面） |
| B18 | WeaselServerImpl.cpp:450-455, 496-519 | worker 可能在 `_RegisterWorker` 之前完成 `_RemoveWorker`+`_FinalizePipe`：监听线程随后把**已关闭**句柄注册进 m_workers；停机 `DrainWorkers` 对其 CancelIoEx/DisconnectNamedPipe——若句柄值已被复用为新管道实例则误伤活连接。仅停机期有影响。修法：注册与移除共用同一临界区（先占位后启动线程） | 新发现 |
| B19 | WeaselPanel.cpp:1261-1264 | `MoveTo` 的 marshal `PostMessage(WM_WEASEL_MOVETO, pRc)` 未检查返回值（`ApplyUpdate:1318`/`ApplyStyle:1353` 都检查并 delete）：窗口销毁中投递失败泄漏 RECT | 新发现 |
| B20 | WeaselIPC/Configurator.cpp:17-21 | `if (!m_pTarget->p_context || key.size() < 2) return;` 检查的是 p_context，随后解引用 `p_config->inline_preedit`。当前生产调用点（EditSession 有 config；其余 context/config 皆空）恰好不触发 NULL 解引用，但任何"有 context 无 config"的新调用点即崩（test/TestResponseParser 正是此形态）。改正检查 p_config | 新发现 |
| B21 | WeaselIPC/Deserializer.cpp:13-28 | `s_factories` 静态 map 懒初始化无锁：同进程多 UI 线程首次并发构造 ResponseParser 时并发 insert → UB。概率低（仅各线程首次解析）。改为函数内 static const map 或 call_once | 新发现 |
| B22 | RimeWithWeasel.cpp:549、RimeWithWeasel.h:89-97 | `_UpdateUI(0)`（StartMaintenance/EndMaintenance 路径）经 `get_session_status(0)` 向 `m_session_status_map` 插入 key 0 的死 SessionStatus，常驻不清；未知/陈旧 ipc_id 亦经 `to_session_status`/`get_session_status` 的 `operator[]` 静默插入（旧 2.9 同类仍未修复），恶意/异常客户端可撑大 map。改 find+早退 | 旧 2.9 同类 |
| B23 | StandardLayout.cpp:6-12；RimeWithWeasel.cpp:871-873 | `swprintf_s<128>`：label 格式化超 128 触发 CRT invalid-parameter 直接终止进程（不可捕获）；PREVIEW_ALL 处格式串 `label_text_format` 直接来自用户 yaml，含非常规格式符即 UB。建议换 `std::format`/snprintf+截断，并校验格式串仅含 `%s` | 旧 P3 未修复 |
| B24 | WeaselPanel.cpp:1003 | `DoPaint` 首行 `ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED)` 每帧执行（等价每帧一次带 SWP_FRAMECHANGED 的 SetWindowPos）。移到 OnCreate 一次性设置 | 旧 2.20 未修复 |
| B25 | WeaselPanel.cpp:1088-1091 | `EndDraw` 失败后 `_InitFontRes(true); Refresh();` 仍继续 `_LayerUpdate` 送上无文字帧，且此时 `m_ctx==m_octx` 使 Refresh 不重绘 → 设备丢失闪空白帧 | 旧 2.21 未修复 |
| B26 | DirectWriteResources.cpp:98-136 | `init_font` lambda 的形参 `wrap` 从未使用，`:126` 恒用捕获的 `wrapping`：pPreeditTextFormat（:136 意图传 `wrapping_preedit`）实际拿到候选词的 WHOLE_WORD 换行，`style/max_width` 下 preedit 不逐字换行 | 旧 2.18 未修复 |
| B27 | FullScreenLayout.cpp:68-123 | `AdjustFontPoint` 反复改写共享 pDWR 的字号；退出全屏后 `_InitFontRes` 因 style/DPI 未变不重建，普通候选窗沿用被缩小的字体（B7 的另一触发源） | 旧 N12 未修复 |
| B28 | WeaselServerImpl.cpp:307-315 | 每键 `GetProcAddress(m_hUser32Module, "PhysicalToLogicalPointForPerMonitorDPI")` 且不判空（靠进程级 Win8.1 gate 兜底）。缓存函数指针 | 旧 P3 未修复 |
| B29 | RimeWithWeasel.cpp:27-31 | `_GenerateNewWeaselSessionId(SessionStatusMap sm, ...)` 按值拷贝整个会话表（AddSession 路径）。改 `const SessionStatusMap&` | 旧 6.4.3 未修复 |
| B30 | RimeWithWeasel.cpp:1462-1463 | `stat.schema_name = u8tow(status.schema_name); stat.schema_id = u8tow(status.schema_id);` 未判空，NULL 构造 `std::string` 是 UB（_Respond:791 同处已判空，此处漏） | 旧 N20 未修复 |
| B31 | WeaselUtility.h:14-32 | `getUsername` 第二次 `GetUserName(username, &len)` 未检查返回值：失败时 len 不变照样走成功路径，用未初始化的 `new wchar_t[]` 构造 wstring。检查第二次返回值 | 新发现 |
| B32 | RimeWithWeasel.cpp:177 | `create_session()` 返回 0（rime 初始化失败/维护中）未检查，session_id=0 进入会话表，后续 5+ 次 rime 调用全部静默失败，客户端拿到一个"假会话"。判 0 时走 EndMaintenance/返回 0 | 新发现 |
| B33 | RimeWithWeasel.cpp:394-417, 689, 563 | OnNotify 与 _ShowMessage/_UpdateUI 共用非递归 `m_notifier_mutex`；若 `get_state_label` 同步触发通知回调则自锁死锁（待验证，取决于 librime 实现）。`m_message_*` 为 static 成员跨实例共享（单实例进程内无实际影响） | 旧 N19，待验证 |
| B34 | WeaselClientImpl.h:45 | `UINT session_id` 被多 UI 线程宿主的多个线程读写（管道句柄是 TSS 的，session_id 却共享），非原子，形式 UB；x86 对齐 UINT 实际原子，建议 `std::atomic<UINT>` 或按线程持有 | 新发现 |
| B35 | 见下 | 杂项边界：① HorizontalLayout.cpp:256（`row_of_candidate[i+1]` 在 i==count-1 且 count==100 时读 `row_of_candidate[100]` 越界；VHorizontalLayout.cpp:564/594 同型）② 三个布局 `CSize sg` 在 candidates_count==0 时未初始化即读（VerticalLayout.cpp:11-21 等，读值后续恰好未使用，属 UB-but-benign）③ RimeWithWeasel.cpp:267 `UpdateColorTheme` 末尾 `m_ui->SetStyle(...)` 无 `m_ui` 判空（同函数上文 :237 有判空）④ WeaselUtility.h:265-269 `DebugStream<<(std::string)` 用 acptow 而注释写 utf-8（与 const char* 分支不一致，仅影响日志）⑤ WeaselPanel.cpp:1335-1342 候选缩写按 wchar 截断可切开代理对（emoji 变 U+FFFD）（旧 P3 未修复）⑥ WeaselPanel.cpp:28-38 `LoadIconNecessary` 图标文件加载失败被缓存直到路径变化才重试（旧 P3 未修复）⑦ WeaselClientImpl.cpp:44-46 `Connect` 忽略 ServerLauncher 参数，死服务端只能靠 autorun 兜底（旧 P3 未修复） | 混合 |
| B36 | WeaselUtility.h:144-184 | `escape_string/unescape_string` 每次调用构造一个 stringstream：服务端 `_GetCandidateInfo`/`_Respond` 每键 3N+2 次（N=候选数，candies/labels/comments + preedit + commit），客户端 `ContextUpdater` 再 3N 次。改为 `std::wstring res; res.reserve(input.size());` 直接 append，每键省 ~6N 次流构造与 locale 查询 | 新发现（perf） |

---

### 5. 已排查、不构成问题的点（避免后人重复排查）

1. **`_UpdateUI` 传给 `m_ui->Update` 的 Context 不含 preedit/候选（`RimeWithWeasel.cpp:539-557`），`_GetContext`（:1493）是死代码**——初看像丢候选的 P1。经对照上游 rime/weasel 0.15.0/0.16.2/master：0.16.2 起 TSF 会话的候选窗由**应用进程内**的 UI 渲染（`WeaselTSF` 的 `CCandidateList::_ui->Create` + `EditSession` 里 `GetResponseData` 解析喂入），服务端 `m_ui` 只承担提示窗（aux）、状态图标与维护提示；`_IsSessionTSF` 与 `_GetContext` 调用在随后的上游重构中被移除，fork 与上游 master 一致。**非缺陷**（`_GetContext` 可作为死代码清理）。
2. 服务端读请求 body 落点 `buffer[0]`（`ReceiveBuffer`）与客户端 `HandleResponseData` 已对称，旧 1.3 已修复；`Transact` 的 `_Ensure` 失败路径、`_Send` 不重发、`_Connect` 有界等待（N3）、`ERROR_PIPE_CONNECTED`（1.1）均复核为已修复。
3. `WakeListener` 自连接唤醒 + `DrainWorkers` CancelIoEx 的停机序（ec079ba）主体正确（残余竞态见 B18）。
4. `f8628c3` 的 ctx/status/style marshal（`WeaselPanel::ApplyUpdate/ApplyStyle/MoveTo/Refresh/RedrawWindow` + `_IsUiThread`）实现正确，PostMessage 失败路径（除 B19 的 MoveTo）均有防泄漏处理。
5. `WeaselTrayIcon` 的 pending/refresh 合并与 `DisableRefresh` 等待 `ApplyRefresh` 完成的停机序正确（数据竞争仅剩 B2 的读侧）。
6. `escape/unescape` 协议转义两侧对称；`blend_colors`/`_RimeGetColor` 的十六进制与 3/4/6/8 位色码解析边界完整。
7. `_Respond` 的 header 先行拼装（`actions` 列表 + `body.reserve(4096)`）当前实现无反复头插问题。

### 6. 修复优先级建议

1. **B1**（marshal Show/Hide/ShowWithTimeout）——与 f8628c3 是同一件事的收尾，风险低、消灭全局死锁面。
2. **B2 + B14 + B11c** 一批（托盘快照改由调用方数据生成、删 explorer 线程、缓存 client_app）——一次改动同时消竞态、线程与每键 rime 交叉。
3. **B3/B4/B20/B30/B19** 一行级修复（substr 参数、vec.size 校验、守卫指针、判空、PostMessage 检查）。
4. **B7/B8/B9/B10**（空 font_face 兜底、HR 收敛 + DoPaint 防护、highlighted clamp、成员初始化）——崩溃面收口。
5. 性能批次：B13（零风险）→ B11(a)(b)（直通键早退、get_status 复用）→ B12（模糊缓存、layout 复用）。
6. 其余 P3 择机；B17/B18 随下一次 PipeServer 改动顺手处理。

---

## 3. 本地验证结果（verifier 填写）

> 待验证。

---

## 4. 修复进度记录

> 待开始。每个批次记录：批次号、修复项、commit、测试情况。
