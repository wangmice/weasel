# Weasel 代码审查与修复跟踪报告（2026-09-14）

- **基线**：HEAD = `acf9c6b`，工作区干净（仅未跟踪 `.vscode/`）
- **方法**：两路并行深度静态审查（A 路：WeaselTSF / WeaselServer / WeaselSetup / WeaselDeployer / test；B 路：WeaselIPC / WeaselIPCServer / RimeWithWeasel / WeaselUI / include 自研头文件），对照旧报告 `weasel_CODE_REVIEW_20260906.md` 与其后约 25 个修复 commit 剔除已修复项；随后逐项本地验证、分批修复。
- **状态标记**：验证列 ✅ 已本地复现 / ✔ 代码核实 / ⚠ 未能完全验证 / ❌ 被推翻 / —（非 bug 无需验证）。修复列 未修复 / 🔧 批次N / ✅ 已修复（commit）。

## 0. 总索引

### A 路新发现

| 编号 | 级别 | 类型 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|---|
| A1 | P2 | bug | WeaselTSF/TextEditSink.cpp:35-44 | OnEndEdit 泄漏 GetSelection 返回的 ITfRange（合成期每击键一次） | ✔ | ✅ 已修复（4ede762（含 K16），批次6） |
| A2 | P2 | bug | WeaselTSF/CandidateList.cpp:230-234,289-291 | Destroy 不清 _uiStarted，StartUI 早退 → 本组合期候选窗永久丢失 | ✔ | ✅ 已修复（af06ec9，批次6） |
| A9 | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘快照在管道线程读 UI style_/status_（wstring）数据竞争（= B2，合并处理） | ✔ | ✅ 已修复（cef6c26（=B2），批次4） |
| A3 | P3 | bug | WeaselTSF/CandidateList.h:85 | uiid 未初始化即传入 UpdateUIElement | ✔ | ✅ 已修复（42eb9ff（含 A4），批次6） |
| A4 | P3 | bug | WeaselTSF/Compartment.cpp:75-89 | _Unadvise 对 null _compartment 解引用；_cookie 未初始化 | ✔ | ✅ 已修复（42eb9ff（含 A3），批次6） |
| A5 | P3 | bug | WeaselTSF/DisplayAttribute.cpp:38-39 | 空 range 时对可能 null 的 _pComposition 解引用（潜在） | ✔ | ✅ 已修复（8266e0b，批次13） |
| A6 | P3 | bug | WeaselTSF/WeaselTSF.h:239, WeaselTSF.cpp:152 | _gaDisplayAttributeInput 未初始化且初始化失败被忽略 | ✔ | ✅ 已修复（7b2b021，批次13） |
| A7 | P3 | bug+perf | WeaselTSF/LanguageBar.cpp:403-419 | 每键无条件读写 compartment；读取失败回写会清掉无关转换位 | ✔ | ✅ 已修复（d09c18e，批次15：bug 部分读失败不回写 + perf 部分稳态零写） |
| A8 | P3 | 死代码 | WeaselTSF/WeaselTSF.cpp:13-20 | error_message（模态框+非线程安全 static）无调用者 | ✔ | ✅ 已修复（37f0da8，批次12） |
| A10 | P3 | bug | WeaselServer/WeaselTrayIcon.cpp:22-38 | 栈上 CIcon 句柄存入 m_tnd.hIcon 后悬垂 | ❌ | 未修复 |
| A11 | P3 | bug | WeaselServer/SystemTraySDK.cpp:427-439 | SetIconList(HICON*,UINT) 差一越界（无调用者） | ✔ | ✅ 已修复（49406da，批次18） |
| A12 | P3 | bug | WeaselServer/SystemTraySDK.cpp:823-832,694-697 | 菜单句柄泄漏 / 子菜单双重销毁 | ✔ | ✅ 已修复（9d16c70，批次18） |
| A13 | P3 | bug | WeaselSetup/WeaselSetup.cpp:94-111 | /i 流程取消选项对话框仍继续安装；_has_installed 过期 | ✔ | ✅ 已修复（89b3e1a，批次8） |
| A14 | P3 | bug | WeaselSetup/WeaselSetup.cpp:68-76 | 注册表字符串未强制 NUL 终止即构造 wstring | ✔ | ✅ 已修复（2e67458，批次8） |
| A15 | P3 | perf | WeaselTSF/EditSession.cpp:8-14 | 每击键堆分配 shared_ptr<Context>+Config+parser | — | ✅ 已修复（1c3e8ec，批次15：parser 复用；Context 分配经考证为承重保留） |
| A16 | P3 | bug | WeaselDeployer/UIStyleSettings.cpp:42-58 等 | 预览路径用 ACP 解码 UTF-8，非 ASCII 用户名下必失败 | ✅ | ✅ 已修复（0f1574a，批次7） |

### A 路：旧报告已知且确认仍未修复（K 系列）

| 编号 | 级别 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|
| K1 | **P1** | WeaselDeployer/Configurator.cpp:220-228 | SyncUserData 失败不调 EndMaintenance → 服务端永久维护态、全系统禁输 | ✔ | ✅ 已修复（f3a888f，批次1） |
| K2 | P2 | WeaselTSF/KeyEventSink.cpp:7-60 | static 三件套跨实例/线程共享；pfEaten 未写即存 static | ✔ | ✅ 已修复（7e41ec3，批次12） |
| K3 | P2 | WeaselTSF/KeyEvent.cpp:44-51 | ConvertKeyEvent 函数级 static buf/table 非线程安全；扫描码传参错误 | ✔ | ✅ 已修复（09b2e4c，批次12） |
| K4 | P2 | WeaselTSF/CandidateList.cpp:129 | SysAllocStringLen(size()+1) BSTR 长度差一 | ✅ | ✅ 已修复（cd59189，批次2） |
| K5 | P2 | WeaselTSF/Register.cpp:10,226-231 | "Microsft" 拼写 + HKCR 下清理对真实 TIP 键结构上无效 | ✔ | ✅ 已修复（20d0f4c，批次13） |
| K6 | P2 | WeaselDeployer/SwitcherSettingsDialog.cpp:161 等 | new[] 配标量 delete（UB） | ✔ | ✅ 已修复（55221cb，批次7） |
| K7 | P2 | SwitcherSettingsDialog.cpp:20-23; UIStyleSettings.cpp:5-8 | schema list / settings 无对应 destroy | ✔ | ✅ 已修复（6258af1，批次7） |
| K8 | P2 | SwitcherSettingsDialog.cpp:114-155 | 未初始化 HKEY、无条件 close、INFINITE 等待、无 NUL | ✔ | ✅ 已修复（03ea36a，批次7） |
| K9 | P2 | WeaselDeployer/DictManagementDialog.cpp:109-123 | CP_ACP 解码 UTF-8 + LB_GETTEXT 缓冲可溢出 | ✔ | ✅ 已修复（dd4b6e9+0cd2936，批次16：①编码②缓冲分项提交） |
| K10 | P2 | DictManagementDialog.cpp:13,25 | STA 线程无条件 CoUninitialize 拆主循环计数 | ✔ | ✅ 已修复（b01eeea，批次16） |
| K11 | P2 | WeaselSetup/imesetup.cpp:178-464 | WOW64 重定向 4 处提前 return 不恢复；install() 忽略文件拷贝结果 | ✔ | ✅ 已修复（ec3678e，批次8） |
| K12 | P2 | imesetup.cpp:364-375 | regsvr32 退出码不检查，失败仍报成功 | ✔ | ✅ 已修复（f409c78，批次8） |
| K13 | P2 | imesetup.cpp:514-517 | 卸载不清 HKCU 配置；RegDeleteKey 有子键即失败 | ✔ | ✅ 已修复（d5c6941，批次17） |
| K14 | P2 | WeaselSetup/WeaselSetup.cpp:109-111 | 改 profile 只写注册表不重注册 TSF profile | ✔ | ✅ 已修复（daced50，批次8） |
| K15 | P2 | WeaselSetup/WeaselSetup.cpp:209-212 | /userdir 引号不剥离 | ✔ | ✅ 已修复（25c1efc，批次8） |
| K16 | P2 | WeaselTSF/Composition.cpp:163,166-182 | GetTextExtent 会话泄漏 pRange 与 selection.range（每击键） | ✔ | ✅ 已修复（4ede762（含 A1），批次6） |
| K17 | P2 | WeaselTSF/CandidateList.cpp:160-163 | SetSelection 不校验 nIndex（下游裸数组越界，= B9 同族） | ✔ | ✅ 已修复（41dbd23，批次6） |
| K18 | P2 | WeaselDeployer/Configurator.cpp:103-106 | && 短路：取消方案对话框静默跳过 UI 风格设置 | ✔ | ✅ 已修复（29c1a62，批次7） |
| K19 | P2 | Configurator.cpp:141-155 | deploy 后不 join_maintenance_thread 即 EndMaintenance | ✔ | ✅ 已修复（04b7775，批次7） |
| K20 | P2 | test/TestWeaselIPC/TestWeaselIPC.cpp:143-146 | AddSession 签名不 override，测试服务端会话计数不增长 | ✔ | ✅ 已修复（63d3cae，批次9） |
| K21 | P3 | WeaselTSF/WeaselTSF.cpp:177-190 | 每次线程焦点切换读注册表 + 2 次 IPC 往返 | — | ✅ 已修复（2666c70，批次15：注册表 TTL 缓存，IPC 保留有据） |
| K22 | P3 | 多处 | P3 杂项族（详见 A 路报告 §3 表） | ✔ | ✅ 已修复（批次13/14/16/17/18 分模块收口，杂项族全部完成） |
| K23 | P3 | perf | 每键 compartment/语言栏/图标读盘等性能族 | — | ✅ 已修复（00f7695+a34af93+78d08c1，批次15：a/b/c 分项提交；compartment 读写部分随 A7） |
| K24 | P3 | WeaselTSF/KeyEventSink.cpp:65-74 | 失焦即清空已输入编码，切回不恢复 | ✔ | 不修（有据，上游一致/防串扰设计，见 §4 批次12） |

### B 路发现

| 编号 | 级别 | 类型 | 位置 | 描述 | 验证 | 修复 |
|---|---|---|---|---|---|---|
| B1 | **P1** | bug | WeaselUI/WeaselUI.cpp:50-94 等 | Show/Hide/ShowWithTimeout 未 marshal，管道线程持 g_api_mutex 跨线程 ShowWindow → 与消息线程互等死锁 | ✔ | ✅ 已修复（1878ff6，批次1） |
| B2 | P2 | bug | WeaselServer/WeaselTrayIcon.cpp:40-53 | 托盘刷新在管道线程读 ui.style_/status_（= A9） | ✔ | ✅ 已修复（cef6c26（=A9），批次4） |
| B3 | P2 | bug | WeaselUI/StandardLayout.cpp:98 | substr(start,end) 第二参误当长度（旧 V1 已复现，此处漏修） | ✅ | ✅ 已修复（22cf009，批次2） |
| B4 | P2 | bug | WeaselIPC/ContextUpdater.cpp:55-62 | 守卫 size()<2 却读 vec[2] 越界（旧 V2） | ✅ | ✅ 已修复（6d43edc，批次2） |
| B5 | P2 | bug | WeaselIPC/Deserializer.h:8-16 | 反序列化异常在输入线程弹模态 MessageBox | ✔ | ✅ 已修复（8083f76，批次9） |
| B6 | P2 | bug | include/PipeChannel.h:64-67 | TSS 管道句柄退出只 delete 不 CloseHandle | ✔ | ✅ 已修复（3b7f9c9，批次10） |
| B7 | P2 | bug | WeaselUI/DirectWriteResources.cpp:103-106 | font_face 空串时 ws_split[0] 越界（MSVC 空 vector） | ❌ | 未修复 |
| B8 | P2 | bug | include/WeaselUtility.h:315-321 等 | HR() 对 S_FALSE 也抛且 UI 路径无局部 catch → 服务整体退出 | ✔ | ✅ 已修复（22cf921+7a67c20，批次3） |
| B9 | P2 | bug | WeaselUI/VerticalLayout.cpp:215 等 | highlighted 无上限校验直接索引裸数组 | ✔ | ✅ 已修复（35c1b48，批次3） |
| B10 | P2 | bug | WeaselUI/WeaselPanel.h:158-162 | m_istorepos/m_offsetys 等未初始化即读 | ✔ | ✅ 已修复（2570084，批次3） |
| B11 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp | 每键 ~7 次 rime 交叉：直通键无早退、get_status×2、get_property 每键 | — | ✅ 已修复（c=ad4e3fa 批次4；a/b=eb5900a 批次5） |
| B12 | P2 | perf | WeaselUI/WeaselPanel.cpp 等 | 每键整窗重算重绘：布局重建、双 layout、全幅模糊 | — | 未修复 |
| B13 | P2 | perf | include/WeaselIPCData.h:103,166 | notequal/operator== 按值深拷贝候选向量（每键 6 份） | — | ✅ 已修复（a3d94fa，批次5） |
| B14 | P2 | perf | RimeWithWeasel/RimeWithWeasel.cpp:73-87 | explorer.exe 每键 detached 线程 + Sleep(100) | — | ✅ 已修复（ad4e3fa（含 B11c），批次4） |
| B15 | P3 | bug | WeaselIPC/WeaselClientImpl.cpp:145-191 | StartSession 失败 body 残留，下次拼双份客户端信息 | ✔ | ✅ 已修复（4d8777f，批次9） |
| B16 | P3 | bug | include/PipeChannel.h:171-184 | body>64KB 时 failbit → 静默只发头不发 body | ✅ | ✅ 已修复（8314d82，批次9） |
| B17 | P3 | bug | WeaselIPCServer/WeaselServerImpl.cpp:445-461 | Listen catch(...) 后无退避，管道创建持续失败时 100% CPU | ✔ | ✅ 已修复（7c086f0，批次1） |
| B18 | P3 | bug | WeaselServerImpl.cpp:450-519 | worker 先于 _RegisterWorker 结束 → m_workers 残留已关闭句柄 | ✔ | ✅ 已修复（7aa084b，批次1） |
| B19 | P3 | bug | WeaselUI/WeaselPanel.cpp:1261-1264 | MoveTo marshal 不检查 PostMessage 返回值泄漏 RECT | ✔ | ✅ 已修复（cc3507b，批次2） |
| B20 | P3 | bug | WeaselIPC/Configurator.cpp:17-21 | 守卫检查 p_context 却解引用 p_config | ✔ | ✅ 已修复（116238a，批次2） |
| B21 | P3 | bug | WeaselIPC/Deserializer.cpp:13-28 | s_factories 无锁懒初始化 | ✔ | ✅ 已修复（a6d9e9b，批次9） |
| B22 | P3 | bug | RimeWithWeasel.cpp:549 等 | operator[] 向会话表插入死条目 | ✔ | ✅ 已修复（418d24a，批次11） |
| B23 | P3 | bug | WeaselUI/StandardLayout.cpp:6-12 等 | swprintf_s 超长/非法格式符 → CRT 直接终止进程 | ✅ | ✅ 已修复（f96f9f2，批次19） |
| B24 | P3 | bug | WeaselUI/WeaselPanel.cpp:1003 | DoPaint 每帧 ModifyStyleEx | ✔ | ✅ 结案（2ee9b36 首帧化 → 0da2dea 回退：panel 对象可复用于重建的 HWND，样式切换必须每帧执行；每帧成本仅一次样式比较，代码注释已写明。保留每帧实现） |
| B25 | P3 | bug | WeaselPanel.cpp:1088-1091 | EndDraw 失败仍送无文字帧 | ✔ | ✅ 已修复（9df83ae，批次3） |
| B26 | P3 | bug | DirectWriteResources.cpp:98-136 | init_font 忽略 wrap 形参，preedit 换行失效 | ✔ | ✅ 已修复（23d69ba，批次3） |
| B27 | P3 | bug | FullScreenLayout.cpp:68-123 | AdjustFontPoint 永久污染共享字号 | ✔ | ✅ 已修复（d43914f，批次4） |
| B28 | P3 | bug | WeaselServerImpl.cpp:307-315 | 每键 GetProcAddress 且不判空 | ✔ | ✅ 已修复（37e5eda，批次10） |
| B29 | P3 | perf | RimeWithWeasel.cpp:27-31 | 会话表按值拷贝 | — | ✅ 已修复（f354257，批次5） |
| B30 | P3 | bug | RimeWithWeasel.cpp:1462-1463 | schema_name/id 未判空构造 std::string UB | ✔ | ✅ 已修复（d416100，批次2） |
| B31 | P3 | bug | include/WeaselUtility.h:14-32 | getUsername 二次调用失败未校验 | ✔ | ✅ 已修复（7aa5fb9，批次10） |
| B32 | P3 | bug | RimeWithWeasel.cpp:177 | create_session 返回 0 未检查全链路静默失败 | ✔ | ✅ 已修复（b9d89f0，批次11） |
| B33 | P3 | bug | RimeWithWeasel.cpp:394-417 | 非递归互斥自锁风险（待验证） | ❌（librime 源码佐证被推翻，见 §4 批次11） | 不修（有据，35b9789 留锁约束注释，批次11） |
| B34 | P3 | bug | WeaselIPC/WeaselClientImpl.h:45 | session_id 跨线程非原子 | ⚠ | ✅ 已修复（090634b，批次10） |
| B35 | P3 | bug | 多处 | 杂项边界（见 B 路报告 P3 表） | ✔ | ✅ 已修复（批次11③/10④⑦/19①②⑤⑥，杂项族全部完成） |
| B36 | P3 | perf | include/WeaselUtility.h:144-184 | escape/unescape 每串一个 stringstream（每键 ~6N 次） | — | ✅ 已修复（53541b8，批次5） |
| B37 | P3 | bug | WeaselUI/WeaselPanel.cpp | _DrawCandidates 的 comments.at(i)/GetLabelText 的 labels.at(id) 在向量短于 candies 时抛 out_of_range（批次3 测试中实际触发；现被 B8 防护兜住不再致命） | ✔（批次3 实测触发） | ✅ 已修复（9b71523，批次4） |

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

## 3. 本地验证结果（verifier 已填写）

- **基线核对**：HEAD = `e3f69c3` = `acf9c6b` + 本报告的 docs commit，源码与审查基线一致；工作区干净（仅未跟踪 `.vscode/`）。
- **验证环境**：MSVC 19.43.34808（VS2022，/MDd 调试 CRT），boost 1.84（仓库 `deps/`）。
- **复现程序**：`Z:/Temp/weasel_mgmt/verify/`（`repro_*.cpp` + `build.cmd`，未入仓库；B3/B4/K4/A16/A10/B16/B23/K6）。
- **统计**：bug 条目共 67 项（A 路 15 + K 路 22 + B 路 30；A9 与 B2 为同一缺陷的两行）。✅ 运行时复现 6 项；✔ 代码核实 57 项；⚠ 未能完全验证 2 项；❌ 推翻 2 项。perf 条目（A15、K21、K23、B11-B14、B29、B36）按约定不验证，保持 —。

### 3.1 验证总览表

| 编号 | 结论 | 验证方式 | 一句话依据 |
|---|---|---|---|
| A1 | ✔ | 代码 | TextEditSink.cpp:35-44：`GetSelection` 返回的 `tfSelection.range`（TSF 契约 AddRef）在所有路径均未 Release，同函数内 `pRangeComposition` 的 Release 反证并非遗漏约定 |
| A2 | ✔ | 代码 | Destroy()（:230-234）仍注释着 EndUI、不清 `_uiStarted`；StartUI()（:289-291）因之早退，`_MakeUIWindow` 全仓库唯一重建入口；触发链三条（OnCompositionTerminated 保留 UIElement 后 OnKillThreadFocus/OnSetFocus(FALSE)/Abort() → `_AbortComposition` 在 `_IsComposing()==false` 时仅走 `Destroy()`）逐行核实成立，机制确凿；仅宿主出现频率（路径 1）无法统计 |
| A3 | ✔ | 代码 | CandidateList.h:85 `DWORD uiid;` 无初始化、构造函数亦不初始化；`_UpdateUIElement`（:272-287）无 `_uiStarted` 守卫，`UpdateUI`（EditSession.cpp:58）每键无条件调用，首次 StartUI 成功前即传入未初始化值 |
| A4 | ✔ | 代码 | Compartment.cpp:79 `_Unadvise` 首行对可能为 null 的 `_compartment` 调 QueryInterface；`_Advise` 半途失败（:58/:62）不清场；`_InitCompartment`（:196-204）第一个 Advise 结果存 `DWORD hr` 被第二个覆盖后仍判成功，留下空 sink 供 Deactivate 解引用；Compartment.h:26 `_cookie` 无初始化 |
| A5 | ✔ | 代码 | DisplayAttribute.cpp:38-39 `pRangeComposition==nullptr` 分支直接 `_pComposition->GetRange`，`_pComposition` 可能为 null；唯一调用点（Composition.cpp:289）恒传非空，与报告"潜在"定性一致 |
| A6 | ✔ | 代码 | WeaselTSF.h:239 原子未初始化、构造函数不初始化；WeaselTSF.cpp:152 `_InitDisplayAttributeGuidAtom()` 返回值被忽略（注释自认部分应用失败）；DisplayAttribute.cpp:50 把该值写入 GUID_PROP_ATTRIBUTE |
| A7 | ✔ | 代码 | LanguageBar.cpp:406-416：读取失败 flags 保持 0，仅按 ascii/full_shape 重建回写 → ROMAN/KATAKANA 等位被清零；:418 无条件 `UpdateWeaselStatus`（:262-264 OnUpdate 恒调）；调用点 EditSession.cpp:16 每键 |
| A8 | ✔ | 代码 | 全仓库 grep `error_message` 仅 WeaselTSF.cpp:13 定义一处，零调用者；函数体仍是模态框 + static 回绕判定 |
| A9 | ✔ | 代码 | WeaselTrayIcon.h:80-81/ctor:13-14 `m_style/m_status` 为绑定 `ui.style_/ui.status_` 的引用（WeaselUI.h:68-69 确为成员引用）；读侧 RequestRefresh:45 在管道工作线程（RimeWithWeasel.cpp:560 → _UpdateUICallback；explorer 分支更在 detached 线程延迟 100ms）；写侧 UI 线程 OnApplyStyle:1251 `m_style=*pStyle`、_ApplyUpdate:1334 `m_status=status`（WeaselPanel 的 m_style/m_status 即 ui 成员引用）——非原子并发读写 std::wstring，数据竞争成立；m_state_mutex 只保护 pending 状态，不覆盖此读 |
| A10 | ❌ | 运行时 | 见 §3.2：WTL 一参 LoadIconW 走共享 ::LoadIcon，DestroyIcon 后句柄仍有效（GetIconInfo 成功），m_tnd.hIcon 不悬垂 |
| A11 | ✔ | 代码 | SystemTraySDK.cpp:431 `for (UINT i = 0; i <= nNumIcons; i++)` 读 `pHIconList[nNumIcons]` 差一；全仓库无调用者（仅定义与声明），启用即炸的潜在缺陷属实 |
| A12 | ✔ | 代码 | :827-829 双击分支 `if (!hSubMenu) return 0;` 未 DestroyMenu(hMenu)（对照单击分支 :782-784 有销毁）；:696-697 `DestroyMenu(hSubMenu)` 后 `DestroyMenu(hMenu)`（父菜单递归销毁子菜单 → 二次销毁） |
| A13 | ✔ | 代码 | WeaselSetup.cpp:100-102 `IDOK != DoModal()` 且 `installing` 时不 return，继续以默认 profile 安装；:106 `_has_installed = dlg.installed` 仅 IDOK 分支执行，取消路径沿用旧值 |
| A14 | ✔ | 代码 | :68-69 `WCHAR value[MAX_PATH];` 未清零，`RegQueryValueEx` 恰好填满缓冲时无 NUL 即 `user_dir = value` / `profile = value` 越读 |
| A16 | ✅ | 运行时 | repro_a16.exe：中文目录 UTF-8 字节经 acptow 解码为乱码路径，GetFileAttributes=INVALID_FILE_ATTRIBUTES；u8tow 解码则命中。见 §3.4 |
| K1 | ✔ | 代码 | Configurator.cpp:220-228：sync 失败仅 `CloseHandle(hMutex); return 1;`，跳过 :232-235 的 `EndMaintenance()`。影响面备注见 §3.5 |
| K2 | ✔ | 代码 | KeyEventSink.cpp:7-9 文件级三 static；:37-38 `if (!keyCountToSimulate)` 才写 `*pfEaten`（Caps 模拟期间不写），:60 却无条件 `prevfEaten = *pfEaten` 存入 static |
| K3 | ✔ | 代码 | KeyEvent.cpp:45-46 `static WCHAR buf[8]; static BYTE table[256];` 函数级 static；:51 `ToUnicodeEx(vkey, UINT(kinfo), ...)`——KeyEvent.h:14 `operator UINT32` 返回完整打包值（repeatCount\|scanCode\|标志位），非扫描码 |
| K4 | ✅ | 运行时 | repro_core.exe：3 字符串配 `size()+1` 后 `SysStringLen==4`（旧 V3 复现 + 当前代码 :129 未变 + 本次重跑）。见 §3.4 |
| K5 | ✔ | 代码 | Register.cpp:10 仍为 "Microsft"；:226-231 在 `HKEY_CLASSES_ROOT` 下删 `Software\Microsft\CTF\TIP\{clsid}`——HKCR 仅合并 `HKLM\Software\Classes`，真实 TIP 键在 `HKLM\SOFTWARE\Microsoft\CTF\TIP`（RegisterProfile 落点），该清理即使拼写改对也对真键无效 |
| K6 | ✔ | 代码+运行时 | :161 `new const char*[...]` 配 :176/:180 标量 `delete`，标准层面 UB 成立；运行时备注：MSVC 对平凡可析构元素不加数组 cookie，调试 CRT 不报 _BLOCK_TYPE_IS_VALID，实际无可观察故障（见 §3.5 优先级建议） |
| K7 | ✔ | 代码 | SwitcherSettingsDialog.cpp:20-23 两次 `get_available/selected_schema_list` 填充的 RimeSchemaList 全程无 `free_schema_list`（Populate 还会二跑）；UIStyleSettings.cpp:7 `custom_settings_init` 无对应 destroy（析构为空） |
| K8 | ✔ | 代码 | :114 `HKEY hKey;` 未初始化；:120 RegOpenKey 失败时 :155 仍无条件 `RegCloseKey(hKey)`；:149 UI 线程 `WaitForSingleObject(INFINITE)`；:122 `value[MAX_PATH]` 未强制 NUL |
| K9 | ✔ | 代码 | :110-112 `get_user_data_sync_dir`（UTF-8）经 `MultiByteToWideChar(CP_ACP)` 解码——与 A16 同根（已运行时证明 ACP 解码 UTF-8 必乱）；:121-122 `dict_name[100]` 收 LB_GETTEXT，>99 字符词典名即栈溢出（LB_GETTEXT 不截断整串拷贝） |
| K10 | ✔ | 代码 | OpenFolderAndSelectItem :13 `CoInitializeEx(0, COINIT_MULTITHREADED)` 在 STA 主线程（WeaselDeployer _tWinMain 已 CoInitialize）返回 RPC_E_CHANGED_MODE 不加计数，:25 无条件 `CoUninitialize` 却拆主循环计数，多次调用后主线程 COM 被拆 |
| K11 | ✔ | 代码 | install_ime_file :196/:214/:224/:237 四处 copy_file 失败 return 1 均不 `Wow64RevertWow64FsRedirection`（Revert 仅 :240）；install() :387 把失败累进 retval 后照写全部注册表/WER 键，:463 才检查 retval |
| K12 | ✔ | 代码 | register_text_service :364-380 仅 ShellExecuteExW 失败返回 1；成功启动后不 `GetExitCodeProcess`，regsvr32 注册失败也 return 0 |
| K13 | ✔ | 代码 | uninstall :516-517 `RegDeleteKey(HKLM, WEASEL_REG_KEY/RIME_REG_KEY)`——RIME_REG_KEY 有子键即失败；全程不清理 HKCU `Software\Rime\weasel` 配置 |
| K14 | ✔ | 代码 | CustomInstall 已安装分支（WeaselSetup.cpp:109-111）只写注册表值；profile 重注册（regsvr32→RegisterProfiles 的 enable 位、InstallLayoutOrTip）仅在 install() 内执行 |
| K15 | ✔ | 代码 | :209-212 `res` 直接指向 `lpCmdLine` 前缀后原文，`/userdir:"..."` 引号原样入库；`return SetRegKeyValue(...)` 把 LSTATUS 当退出码 |
| K16 | ✔ | 代码 | CGetTextExtentEditSession::DoEditSession：:172 GetSelection 的 `selection.range` 与 :163/:176 裸 `ITfRange* pRange`（GetRange AddRef）在所有路径（含 :171/:174 早退）均未 Release；调用点 _UpdateCompositionWindow 每键触发（EditSession.cpp:53） |
| K17 | ✔ | 代码 | CandidateList.cpp:160-163 `SetSelection` 直写 `cinfo.highlighted`；下游 `GetCandidateRect(id)`→`_candidateRects[id]`（StandardLayout.h:37/75）裸数组无校验 |
| K18 | ✔ | 代码 | Configurator.cpp:103-106 `(skip_switcher \|\| configure_switcher(...)) && (skip_ui \|\| configure_ui(...))`——取消方案对话框返回 false 时短路，configure_ui 不执行 |
| K19 | ✔ | 代码 | UpdateWorkspace :141-155 deploy/deploy_config_file 后无 `join_maintenance_thread` 即 EndMaintenance（对照 SyncUserData :227 有 join） |
| K20 | ✔ | 代码 | 基类 `AddSession(LPWSTR buffer, EatLine eat = 0)`（WeaselIPC.h:63）vs 测试类 `AddSession(LPWSTR buffer)`（TestWeaselIPC.cpp:143）签名不同且无 override → 隐藏而非重写；ServerImpl::OnStartSession 带 eat 调用命中基类空实现，m_counter 永不增长 |
| K22 | ✔ | 代码(抽样) | 抽样 10 项：Composition.cpp:25,59 成功路径仍返回 E_FAIL ✔；EditSession.cpp:60 返回 TRUE ✔；Compartment.cpp:196-204 首个 Advise 结果被覆盖 + 存 DWORD ✔；TextEditSink.cpp:100-115 text-edit advise 失败时 cookie=INVALID → 下轮清理整块跳过，已成功的 layout sink 泄漏 ✔；WeaselServer.cpp:41-46 user_name[20] 不查返回值 ✔；imesetup.cpp:342-344 throw 无人接 ✔；WeaselDeployer.cpp:47-49 单实例静默 ret=1 ✔；dllmain/WeaselService 各项读码相符 ✔。**一项被推翻**：imesetup.cpp:36-58 ".old.0~9 重启后无人清理"不实——:39/:54 `MoveFileEx(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)` 已排定重启删除。整体族成立 |
| K24 | ✔ | 代码 | KeyEventSink.cpp:65-74 `OnSetFocus(FALSE)` → `FocusOut()+_AbortComposition()`（默认 clear=true）→ `m_client.ClearComposition()` 清空 Rime 编码并销毁候选窗，切回无恢复 |
| B1 | ✔ | 代码(死锁链) | 链条五环全部核实：① 面板窗口属主=服务主线程（WeaselServerApp.cpp:30 `m_ui.Create` 在 :39 `m_server.Run()` 消息循环之前、同线程）；② 管道 worker 在 g_api_mutex 内执行 handler（WeaselServerImpl.cpp:184）；③ worker 路径调未 marshal 的 `m_ui->Hide/Show/ShowWithTimeout`（RimeWithWeasel.cpp:223/369/556/742/1485）→ UIImpl 直接 `panel.ShowWindow/SetTimer`（WeaselUI.cpp:50-81），跨线程 ShowWindow 经 WM_SHOWWINDOW/WM_WINDOWPOSCHANGING 同步 send，需窗口线程泵消息才返回；④ 服务消息线程在 OnColorChange:47/OnEndSystemSession:91/OnCommand:106,112 阻塞于 g_api_mutex，不泵消息；⑤ Refresh/RedrawWindow/MoveTo/ApplyUpdate/ApplyStyle 均 marshal（WeaselPanel.cpp:145/1137/1261/1316/1351）唯 Show 家族缺席——互等死锁成立，概率低但为全局输入冻结，与报告一致。运行时构造（需真实双线程+窗口时序）不可行，按任务约定代码裁决 |
| B2 | ✔ | 代码 | = A9（同一缺陷），托盘读侧证据见 A9 行 |
| B3 | ✅ | 运行时 | repro_core.exe：`substr(3,5)` 得 "defgh"（长 5，应 "de"）；StandardLayout.cpp:98 与旧 V1 复现时代码一致未变；WeaselPanel.cpp:683 已是修正式（`end - start`），"WeaselPanel 已修此处漏修"属实。见 §3.4 |
| B4 | ✅ | 运行时 | repro_b4.exe（MSVC 调试 CRT）：`vector subscript out of range` 断言即崩；ContextUpdater.cpp:55-62 与旧 V2 复现时一致未变。见 §3.4 |
| B5 | ✔ | 代码 | Deserializer.h:11-15 catch archive_exception 后 `MessageBoxA(..., MB_OK \| MB_ICONERROR)` 模态框；运行于宿主输入线程（GetResponseData ← DoEditSession）；ContextUpdater.cpp:73-76 归档构造确在 TryDeserialize 的 try 之外 |
| B6 | ✔ | 代码 | PipeChannel.h:64-67 `thread_specific_ptr<HANDLE>` 无 cleanup 函子，boost 缺省清理=delete 指针本身；`CloseHandle` 仅在 `_FinalizePipe` 执行，线程退出路径不经过；服务端 worker 阻塞 ReadFile 直至宿主进程退出 |
| B7 | ❌ | 运行时 | 见 §3.2：MSVC(19.43) `ws_split(L"")` 产出 1 个空 token 的 vector（size==1），`[0]` 不越界，报告前提（MSVC→空 vector）在本工具链上不成立 |
| B8 | ✔ | 代码 | WeaselUtility.h:316 `if (S_OK != result) throw`——S_FALSE(1) 也抛；HR() 遍布绘制路径（WeaselPanel._TextOut:1444-1465、StandardLayout GetTextSizeDW 5 处、DirectWriteResources ctor），DoPaint 无局部 catch，唯一进程级 catch 在 WeaselServer.cpp:117-122 → 服务退出；WeaselUI.h:129-131 GetLayoutOverhangMetrics 空指针先解引用同样属实 |
| B9 | ✔ | 代码 | Layout.h:15 `id(_context.cinfo.highlighted)` 无 clamp；`_highlightRect = _candidateRects[id]`（Vertical:215、Horizontal:221、VHorizontal:229/451/497/513）；客户端 cinfo 来自 IPC 反序列化（ContextUpdater::_StoreCand → text_wiarchive）无校验 |
| B10 | ✔ | 代码 | WeaselPanel.h:158-162 四成员无初始化，构造函数初始化列表（:60-77）亦无；首读 DoPaint:1015 `if (m_istorepos)` 及鼠标处理器 :329-330/:438-439 等（以垃圾真值守卫再用同样未初始化的 m_offsetys）；写点仅在 _RepositionWindow :1403(adj==true)/:1417(翻转) 与 DoPaint:1015 之后 |
| B15 | ✔ | 代码 | PipeChannel.h:124-137：`_Ensure()` 失败在 try 块**之前** throw → 无 _Reconnect/_Send/ClearBufferStream；WeaselClientImpl.cpp:149 写入的 body 残留在 TSS ChannelContext.write_stream，下次 StartSession 的 `_WriteClientInfo` 在旧流上追加（双份）；`has_body` 残留使下一个无 body 命令经 :172 搭车发残留 body |
| B16 | ✅ | 运行时 | repro_b16.exe（boost 1.84 wbufferstream）：写入超限后 fail()=bad()=1，`tellp()==-1`，代码路径据此得 `body_bytes=0` → 只发头。见 §3.4 |
| B17 | ✔ | 代码 | WeaselServerImpl.cpp:445-461 `for(;;)` 内 catch(...) 仅 `_FinalizePipe` 后立即回到 `_ConnectServerPipe`（CreateNamedPipe），无任何退避 sleep |
| B18 | ✔ | 代码 | worker 尾部 `_RemoveWorker(pipe); _FinalizePipe(pipe)`（:517-518）可与监听线程 `_RegisterWorker(pipe, worker)`（:455）竞争交错：已关闭句柄入表；停机 DrainWorkers :479-480 对其 CancelIoEx/DisconnectNamedPipe，句柄值若被复用则误伤新连接（仅停机窗口） |
| B19 | ✔ | 代码 | WeaselPanel.cpp:1261-1264 MoveTo 的 `PostMessage(WM_WEASEL_MOVETO, pRc)` 不查返回值；对照 ApplyUpdate:1318-1323 / ApplyStyle:1353-1355 均查并 delete |
| B20 | ✔ | 代码 | WeaselIPC/Configurator.cpp:17 守卫 `!m_pTarget->p_context`，:21 却解引用 `p_config->inline_preedit`；生产路径（EditSession 传 config 无 context）恰好不触发，test/TestResponseParser 形态即中 |
| B21 | ✔ | 代码 | Deserializer.cpp:13-24 `if (s_factories.empty()) { Define... }` 无锁；ResponseParser 构造（ResponseParser.cpp:18）每键执行 → 多 UI 线程宿主首次并发解析即并发 insert |
| B22 | ✔ | 代码 | RimeWithWeasel.h:89-94 `to_session_id/get_session_status` 均为 `m_session_status_map[ipc_id]`（operator[] 插入）；StartMaintenance/EndMaintenance → `_UpdateUI(0)`（RimeWithWeasel.cpp:489/:495）→ 插入 key 0 死条目；StartMaintenance 的 `m_session_status_map.clear()`（:487）可周期清掉，EndMaintenance 后再 clear（:497），常驻风险主要来自未知 ipc_id |
| B23 | ✅ | 运行时 | repro_b23.exe：140 字符参数 + `swprintf_s<128>` 触发 CRT 断言 "Buffer too small" 并走 invalid-parameter 路径（无自装 handler 时进程直接终止，实测退出码 3；装 handler 后捕获到 handler 被调、退出码 77），后续语句不可达。见 §3.4 |
| B24 | ✔ | 代码 | WeaselPanel.cpp:1003 DoPaint 首行 `ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED)` 每帧执行（带 SWP_FRAMECHANGED 的 SetWindowPos） |
| B25 | ✔ | 代码 | :1088-1091 `FAILED(EndDraw())` → `_InitFontRes(true); Refresh();` 后落到 :1126 `_LayerUpdate` 照送帧；且此时 `m_ctx==m_octx` 使 Refresh 内 :189 去重跳过重绘 |
| B26 | ✔ | 代码 | DirectWriteResources.cpp:100 lambda 形参 `wrap` 全程未用，:126 恒用捕获 `wrapping`；:136 传 `wrapping_preedit` 被丢弃 → preedit 拿 WHOLE_WORD 而非逐字换行 |
| B27 | ✔ | 代码 | FullScreenLayout.cpp:103-117 AdjustFontPoint 直接 `pDWR->InitResources(...)` 改共享 DirectWriteResources 字号；退出全屏后 WeaselPanel._InitFontRes:204 重建条件 `(m_ostyle != m_style) \|\| (dpiX != dpi)` 不满足 → 缩水字号沿用 |
| B28 | ✔ | 代码 | WeaselServerImpl.cpp:307-315 每次 OnUpdateInputPosition 都 `GetProcAddress(m_hUser32Module, "PhysicalToLogicalPointForPerMonitorDPI")` 且不判空即调两回；Win8.1 gate 仅进程级（WeaselServer.cpp:29） |
| B30 | ✔ | 代码 | RimeWithWeasel.cpp:1462-1463 `u8tow(status.schema_name/schema_id)` 未判空（对照 _Respond:791 对 schema_id 有 `?: std::wstring()`）；NULL 构造 std::string 为 UB，是否可达取决于 rime 契约 |
| B31 | ✔ | 代码 | WeaselUtility.h:24 第二次 `GetUserName(username, &len)` 返回值不查：失败时 len 仍>0 走成功路径，:29 用未初始化 `new wchar_t[]` 构造 wstring |
| B32 | ✔ | 代码 | RimeWithWeasel.cpp:177 `(RimeSessionId)rime_api->create_session()` 无 0 检查；0 会话入会话表，后续 get_status/set_property 全部静默失败，客户端拿到假会话 |
| B33 | ⚠ | 代码+推理 | 见 §3.3：锁结构事实成立，自锁触发链依赖 librime 内部实现，本地无 librime 源码可证 |
| B34 | ⚠ | 代码+推理 | 见 §3.3：非原子 UINT 属实，但单 ClientImpl 的跨线程共享路径未证实 |
| B35 | ✔ | 代码(全项) | ① HorizontalLayout.cpp:81 数组[100] + :256 循环 `i<candidates_count` 读 `[i+1]`，count==100 时越界（VHorizontalLayout.cpp:332/:564/:594 同型）✔；② VerticalLayout.cpp:11-21 `CSize sg;` 在 candidates_count==0 时未初始化即读 `sg.cx` ✔；③ RimeWithWeasel.cpp:267 `m_ui->SetStyle` 无判空（同函数 :237 有）✔；④ WeaselUtility.h:265-269 `operator<<(std::string)` 用 acptow 而注释写 utf-8（与 const char* 分支 :260 不一致，且按值收参）✔；⑤ WeaselPanel.cpp:1335-1342 逐 wchar 截断可切开代理对 ✔；⑥ WeaselPanel.cpp:28-38 LoadIconNecessary 失败结果被缓存直至路径变化 ✔；⑦ WeaselClientImpl.cpp:44-46 Connect 忽略 ServerLauncher 形参 ✔ |

### 3.2 被推翻项（❌）

**A10 —— 托盘图标句柄悬垂：不成立。**
报告推理：栈上 `CIcon icon; icon.LoadIconW(IDI_ZH);` 传入 `CSystemTray::Create` 存入 `m_tnd.hIcon`（SystemTraySDK.cpp:201），Create 返回时 CIcon 析构 `DestroyIcon` → 句柄悬垂，explorer 重启后 `InstallIconPending → NIM_ADD` 复用死句柄。
运行时验证（`Z:/Temp/weasel_mgmt/verify/repro_a10.exe`）：
- WTL 一参 `LoadIconW`（UNICODE 下即 atluser.h:665 `LoadIcon`）走 `::LoadIconW` —— **共享图标**（LR_SHARED 语义，宿主模块常驻则图标常驻）；
- 对共享句柄执行 `DestroyIcon` 返回 1（表面"成功"），但随后 `GetIconInfo` **依然成功**——句柄未被销毁，仍可正常使用；
- 对照组（`LoadImageW` 无 LR_SHARED 的自有图标）DestroyIcon 后 GetIconInfo 失败，证明测试方法有效。
结论：`m_tnd.hIcon` 存的是共享图标句柄，CIcon 析构的 DestroyIcon 对其无实际销毁效果，TaskbarCreated 重注册用的是活句柄。报告描述的悬垂机制在本代码路径上不发生（WTL 在共享句柄上多调一次 DestroyIcon 属无害冗余）。"首次 NIM_ADD 系统已拷贝图标"亦非必要条件。**无需修复。**

**B7 —— font_face 空串 `ws_split[0]` 越界：前提不成立。**
报告推理：MSVC 的 `wsregex_token_iterator` 对空串产出空 vector，`font_face` 为空时 `fontFaceStrVector[0]` 越界崩溃。
运行时验证（`Z:/Temp/weasel_mgmt/verify/repro_b7.exe`，MSVC 19.43.34808 /MDd，逐字复刻 DirectWriteResources.cpp:10-15 的 `ws_split` 并以 `L""` 调用）：
- `ws_split(L"", L",").size() == 1`（得到一个空 token），`fontFaceStrVector[0]` 完全在界内；
- 按原样执行 `:106` 的 `regex_replace(fontFaceStrVector[0], ...)` 正常通过，无断言无崩溃。
结论：本工具链（VS2022，项目仅支持 VS2019/VS2022）上"空串 → 空 vector"的前提为假，`[0]` 不越界；空 font_face 走到 `CreateTextFormat`/`_SetFontFallback` 亦无崩溃路径。旧报告 N13 的该前提在当前 MSVC STL 上应予更正。**无需修复**（给 font_face 一个非空默认值仍可作为健壮性改进，但不是崩溃缺陷）。

### 3.3 未能完全验证项（⚠）

**B33 —— OnNotify 与 _ShowMessage 共用非递归互斥的自锁。**
已核实的部分：`m_notifier_mutex` 为非递归 `std::mutex` 且为 **static 成员**（RimeWithWeasel.h:118）；OnNotify :403 持锁调用 `rime_api->get_state_label`（:412）；_ShowMessage :689 持锁；_UpdateUI :563 亦取该锁。
未能核实的部分：死锁要求 `get_state_label` 在同一线程**同步回调**通知处理器（重入 OnNotify）。仓库内 `librime/` 为空目录（源码未拉取，仅有预编译 7z），无法读 librime 实现裁决。按 librime 公开实现，`get_state_label` 是对方案配置的纯查询（翻译 option 标签），不派发通知；真正会同步触发通知回调的是 `set_option`/部署事件，而那些路径不在持锁段内调用。倾向认为自锁不可达，但无源码佐证，维持 ⚠。static 成员跨实例共享在单实例 server 进程内无实际影响（与报告注一致）。

**B34 —— ClientImpl::session_id 跨线程非原子。**
已核实的部分：WeaselClientImpl.h:45 `UINT session_id;` 确非原子；管道句柄为 TSS 而会话号共享的结构性不对称属实。
未能核实的部分：**单个 ClientImpl 的跨线程共享路径未找到**——每个 WeaselTSF 实例（每 thread manager 一个）持有自己的 `m_client`/ClientImpl，TSF 各 sink 回调均在所属线程；未发现同一实例被多线程并发调用的通路。x86/x64 对齐 UINT 读写实际原子（报告亦自注"实际良性"）。按"形式 UB、无实证并发路径"定性为加固项，维持 ⚠。

### 3.4 运行时复现（✅）汇总

程序目录 `Z:/Temp/weasel_mgmt/verify/`（cl /EHsc /utf-8 /MDd，boost 1.84 取自仓库 deps），关键输出：

| 程序 | 条目 | 关键输出 |
|---|---|---|
| repro_core.exe | B3 | `substr(3,5) on "abcdefgh" -> "defgh" len=5 (expect len=2 "de")` |
| repro_core.exe | K4 | `SysAllocStringLen(3-char str, 3+1): SysStringLen=4`（内含多余 NUL） |
| repro_b4.exe | B4 | `Assertion failed: vector subscript out of range`（进程即崩） |
| repro_b16.exe | B16 | `fail()=1 bad()=1, tellp() = -1, body_bytes = 0 → 只发头` |
| repro_b23.exe | B23 | `Assertion failed: ("Buffer too small", 0)` → invalid-parameter 路径 → 进程终止（退出码 3；装 handler 后确认 handler 被调） |
| repro_a16.exe | A16 | `acptow(utf8) = 鐢ㄦ埛鐩...  exists=0`、`GetFileAttributes=0xffffffff`；`u8tow` 则 `exists=1` |
| repro_a10.exe | A10(❌) | 共享句柄 DestroyIcon 后 `GetIconInfo SUCCEEDED (handle valid)`；自有句柄对照组 FAILED |
| repro_b7.exe | B7(❌) | `ws_split(empty).size() = 1`，后续 `regex_replace(v[0],...)` 正常执行 |
| repro_k6.exe | K6 | `new[]` + 标量 `delete` 在 MSVC 调试 CRT 下**无任何报告**（平凡元素无数组 cookie），UB 仅存在于标准层面 |

### 3.5 验证备注

1. **K1 影响面细化**：服务端在维护态下，已建立的客户端会话确实全部失效且不自愈（客户端 `_Active()` 仍真、不再重发 StartSession，击键一律直通英文），直到宿主应用重建会话；但**新** AddSession（新应用/新会话）会经 `RimeWithWeaselHandler::AddSession` 的 `EndMaintenance()` 自愈。即"全系统禁输"准确说是"所有现存会话禁输，直至应用侧重开会话或重启服务"，仍是 P1 级可用性缺陷，修复建议不变（RAII 配对 EndMaintenance）。
2. **K6 修复优先级**：标准层面 UB 属实，但在 MSVC 上对 `const char*[N]`（平凡可析构）无数组 cookie，实际无任何可观察故障；修复价值是正确性/卫生（改 `delete[]` 或 `std::vector`），非紧急。
3. **K22 抽样反例**：`imesetup.cpp:36-58 .old.0~9 重启后无人清理` 一项与代码不符（`MoveFileEx(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)` 已排定重启删除），族内其余抽样项均成立，K22 整体维持 ✔。
4. **A7 的 perf 部分**（每键 compartment 往返/无条件 OnUpdate）与 K23 重叠，本次仅验证其 bug 部分（读失败清位）。
5. **A9/B2、B1** 属线程/窗口时序缺陷，代码级证据链完整（见总览表），运行时构造需真实双线程+explorer/托盘交互，未尝试。


---

## 4. 修复进度记录

> 待开始。每个批次记录：批次号、修复项、commit、测试情况。

### 批次 1（2026-09-14）：B1、K1、B17、B18

- `1878ff6` fix(WeaselUI): marshal Show/Hide/ShowWithTimeout to the UI thread — B1（P1）。新增 WM_WEASEL_SHOW marshal 通道（enum class PanelVisibility），autohide 收敛进 panel；新增 test/TestWeaselUI（9 用例全过：同线程直调/跨线程 marshal/倒计时/取消/退出）。
- `f3a888f` fix(WeaselDeployer): resume service when user data sync fails — K1（P1）。MaintenanceReleaser RAII 保证 EndMaintenance 在失败/异常路径也被执行。进程级行为，以构建+推理验证。
- `7c086f0` fix(WeaselIPCServer): back off before retrying a failed pipe listen — B17。50ms 退避（interrupt 可打断，不影响停机）；TestPipeChannel 新增忙循环回归用例（负向验证过：去退避则 FAIL）。
- `7aa084b` fix(WeaselIPCServer): register pipe workers before they can finish — B18。_LaunchWorker 在同一临界区内建线程并登记；TestPipeChannel 新增 30 轮瞬断冲击用例。
- 构建：release/debug 全量 build ok；既有测试全过（TestResponseParser 基线即 exit 3，见 B20，非本批引入）。

### 批次 2（2026-09-14）：B3、B4、B20、K4、B19、B30

- `22cf009` fix(WeaselUI): pass a length, not an end offset, to substr in GetPreeditSize — B3。
- `6d43edc` fix(WeaselIPC): require all three cursor fields when parsing ctx.preedit.cursor — B4（守卫改 vec.size()<3 丢弃属性）。TestResponseParser 从基线即 exit 3 修诊断为过时明文协议 + text_wiarchive 构造期异常在 try 外（B5 附带项的位置），改造 test_4/5/6；负向验证：还原 B4 则 test_5 FAIL。
- `116238a` fix(WeaselIPC): guard p_config, not p_context, in Configurator::Store — B20。新增 test_6（有 context 无 config 不崩）；负向验证：还原则 0xC0000005。
- `cd59189` fix(WeaselTSF): stop counting a trailing NUL in candidate BSTR length — K4。抽出 BstrUtil.h 的 AllocBstr 并新增 TestBstrUtil；负向验证：还原 +1 则 4 处 FAIL。
- `cc3507b` fix(WeaselUI): free the marshalled RECT when MoveTo's PostMessage fails — B19。
- `d416100` fix(RimeWithWeasel): null-check schema_name/schema_id before u8tow — B30。
- 构建：release/debug 全量 build ok；8 个测试目标全过（TestWeaselIPC/TestWeaselUI 因历史 targetdir 写法需直跑 exe，均 exit 0）。

### 批次 3（2026-09-14）：B8、B9、B10、B24、B25、B26

- `22cf921` fix: only throw on failed HRESULTs and guard null text layouts — B8(a)：HR() 收敛为 FAILED 才抛；WeaselUI.h 布局助手对空 pTextLayout 返回 E_POINTER（S_FALSE 放行后此守卫成为必需）。
- `7a67c20` fix(WeaselUI): shield panel paint and layout from throwing past WNDPROC — B8(b)：Refresh/DoPaint 整体防护 + _RequestPaintRecovery（清 m_octx、重建资源、限次投递重刷，防自旋）。TestWeaselUI 注入 E_INVALIDARG 验证进程存活与恢复；负向验证（catch 重抛即崩）有效。
- `35c1b48` fix(WeaselUI): clamp out-of-range highlighted on refresh — B9：Refresh 派生 m_candidateCount 处一处收口 clamp。
- `2570084` fix(WeaselUI): default-initialize panel repositioning state — B10。
- `2ee9b36` fix(WeaselUI): apply the layered-style switch only on the first paint — B24：考证出首帧 ModifyStyleEx 是承重样式切换（去点击穿透+补 LAYERED），改首帧一次性标志，OnDestroy 复位。
- `9df83ae` fix(WeaselUI): drop the frame and force a repaint when EndDraw fails — B25。
- `23d69ba` fix(WeaselUI): honor the wrap parameter in init_font — B26：恢复上游 0.16.2 的 wrapping_preedit 逐字换行语义（上游 master 的 lambda 重构引入同 bug）。
- 构建：release/debug 全量 build ok；8 个测试目标全过。
- 批次外新发现登记为 B37（labels/comments 向量与 candies 不等长时 at() 抛异常），排入批次 4。

### 批次 4（2026-09-14）：A9/B2、B14、B11c、B27、B37

- `cef6c26` fix(WeaselServer): snapshot tray state on the server message thread — A9/B2。From(m_style,m_status) 从管道线程的 RequestRefresh 挪到消息线程的 ApplyRefresh 开头，读与 UI 线程写同线程串行化；pending/DisableRefresh 语义保留。
- `ad4e3fa` perf(RimeWithWeasel): drop the explorer tray-delay shim and cache client_app — B14+B11c。考证结论：垫片防御的是 2018 年"工作线程直接同步 Shell_NotifyIcon 与 explorer 互等"的结构，d73f629 拆分 RequestRefresh/ApplyRefresh 后已不存在（上游 master 仍带垫片未修）；SessionStatus 缓存 client_app，get_property 从每键路径消失（顺带消掉 _LoadAppInlinePreeditSet 的 static 残留，旧 N21）。
- `d43914f` fix(WeaselUI): scope fullscreen font adjustment to the fullscreen layout — B27。panel 侧借用边界：全屏布局存活期内允许 pDWR 缩放、换回普通布局时恢复快照，_InitFontRes 重建使快照失效。
- `9b71523` fix(WeaselUI): treat short label/comment vectors as empty, not exceptions — B37。新增 Layout::TextAt 统一收口（越界返回空 Text），三布局 4 处同族 comments.at(i) 一并修复；TestWeaselUI 4 断言 + 负向验证。
- 构建：release/debug 全量 build ok；8 个测试目标全过（TestPipeChannel 44 用例）。

### 批次 5（2026-09-14）：B13、B36、B11(a)(b)、B29（击键路径性能）

- `a3d94fa` perf(IPC): pass candidate vectors and status by const reference in dedup compares — B13。notequal/Status::operator== 改 const 引用（配套补 Text/TextRange/TextAttribute 比较 const 限定）。micro-bench：9 候选去重命中负载 200 万轮 3090.7ms → 131.3ms（23.5×）。
- `53541b8` perf(utility): build escape/unescape with plain string ops, not stringstream — B36。转义语义逐字符一致；TestResponseParser 新增 test_7 往返/不膨胀用例。micro-bench：escape+unescape 200k 次 152.4ms → 17.5ms（8.7×）。
- `eb5900a` perf(RimeWithWeasel): early-exit passthrough keys and reuse fetched rime status — B11(a)(b)。直通键（!handled && !state_changed）跳过 get_context 与整套 UI 刷新；_Respond 快照复用消掉每键第二次 get_status。协议兼容性考证（librime IsComposing/HasMenu 语义 + TSF 客户端按行解析）确认直通键响应字节流不变；顺带收口 _Respond 路径 schema 字段判空（B30 同型）。
- `f354257` perf(RimeWithWeasel): generate session id from a const session map — B29。
- 构建：release/debug 全量 build ok；全部测试目标通过。

### 批次 6（2026-09-14）：A1、K16、A2、K17、A3、A4（WeaselTSF）

- `4ede762` fix(WeaselTSF): release ranges returned by GetSelection in edit callbacks — A1+K16。OnEndEdit 补 tfSelection.range->Release()；CGetTextExtentEditSession 改"恰持一个引用"所有权模型，两处每击键泄漏收口。
- `42eb9ff` fix(WeaselTSF): default-initialize uiid/_cookie and guard unadvised sinks — A3+A4。uiid=0 + _UpdateUIElement 加 !_uiStarted 守卫；_cookie=0 + _Unadvise 判空返回 S_FALSE。
- `af06ec9` fix(WeaselTSF): end the UI element when destroying the candidate window — A2。恢复 Destroy() 的 EndUI()（历史考证：上游 PR #263 的实验遗留注释，PR #268 确立按组合建窗后即成死代码冲突；DestroyAll 已在 5dffa59 恢复、Destroy 为漏网）；状态机注释写明 _uiStarted⟺注册元素、窗口生命周期不变量；与 A3 守卫配套收口僵尸 UpdateUIElement 路径。
- `41dbd23` fix(WeaselTSF): reject out-of-range candidate selection in SetSelection — K17。TSF 源头收口（E_INVALIDARG），与批次 3 UI 侧 clamp（B9）互补。
- 验证：TSF COM 交互无法控制台复现，按构建+推理验证；release/debug 全量 build ok，8 个测试目标全过（注意 debug 需 xmake f -p windows -a x64 -m debug）。

### 批次 7（2026-09-14）：K6、K7、K8、K18、K19、A16（WeaselDeployer）

- `55221cb` fix(WeaselDeployer): drop manual new[]/delete mismatch in schema selection — K6。改 std::vector；顺带 get_schema_id 判空防 NULL 混入 select_schemas。
- `6258af1` fix(WeaselDeployer): free rime schema lists and custom settings — K7。对照 rime_levers_api.h + librime@33e7814 源码确认 schema_list_destroy 只释放条目数组（幂等安全）、custom_settings_destroy 须严格配对；UIStyleSettings 补析构 + 禁拷贝。
- `03ea36a` fix(WeaselDeployer): harden OnGetSchemata registry read and script wait — K8。复用 RegGetStringValue 消除未初始化 HKEY/无条件 close/NUL 问题；INFINITE 改 60s 有界等待（超时告警继续）；补 ShellExecuteExW 与句柄判空。
- `29c1a62` fix(WeaselDeployer): always offer UI style settings after the switcher dialog — K18。短路改两步独立执行；"取消方案但完成 UI 设置"路径现在会保存并部署，退出码结构不变。
- `04b7775` fix(WeaselDeployer): join maintenance thread before resuming service — K19。UpdateWorkspace/DictManagement 复用 K1 的 MaintenanceReleaser（join 在前、EndMaintenance 在析构，异常路径同样恢复）。
- `0f1574a` fix(WeaselDeployer): decode color scheme preview paths as UTF-8 — A16。acptow→u8tow 两处，删误导注释。
- 验证：GUI 路径按构建+推理；release/debug 全量 build ok；8 个测试目标全过（TestPipeChannel 47 用例）。上游 master 三处同题均未修（本仓独立改进）。

### 批次 8（2026-09-14）：K11、K12、K14、K15、A13、A14（WeaselSetup）

- `ec3678e` fix(WeaselSetup): restore WOW64 redirection on early exits and abort install on file failure — K11。Wow64FsRedirectionGuard RAII（幂等）；install() 在 install_ime_file 失败时立即返回，不再继续写注册表/弹成功。
- `f409c78` fix(WeaselSetup): check the regsvr32 exit code when registering the text service — K12。GetExitCodeProcess 检查退出码，失败复用 IDS_STR_ERRREGTSF 报错并跳过 enable_profile。
- `daced50` fix(WeaselSetup): re-register the TSF profile when changing it on an installed system — K14。考证：五 langid 条目安装时已全注册，换 profile 无需 regsvr32，只需启用位/输入法列表调整；实现 switch_registered_profile（先启新、失败保留旧），InstallLayoutOrTip 抽公共 helper（enum class LayoutOrTipAction）。
- `25c1efc` fix(WeaselSetup): strip quotes from the /userdir argument and return explicit status — K15。新增 SetupUtil::unquote_argument；返回值 LSTATUS→0/1。
- `89b3e1a` fix(WeaselSetup): abort the /i install when the options dialog is cancelled — A13。取消一律 return 1（顺带根除 _has_installed 过期路径）。
- `2e67458` fix(WeaselSetup): bound registry string reads to the returned data length — A14。SetupUtil::read_reg_sz 按返回长度定界。
- 验证：WeaselSetup 以 xmake f -a x86 单独构建通过；x64 debug/release 全量 build ok；新增 test/TestWeaselSetup（9 个 unquote + 7 个私有注册表用例），9 个测试目标全过。

### 阶段性收尾（2026-09-14）

应用户要求，修复工作在批次 8 后暂停（批次 9 后于 2026-09-15 补做，见下节）。剩余未修复项见 §0 索引"未修复"标记，主要为：TSF 静态/线程安全族（K2/K3）、TIP 注册清理（K5）、字典管理对话框（K9/K10）、安装器残余（K13）、TSF P3 杂项族（K22/K24 及若干返回值问题）、IPC P3 族（B5/B6/B15/B16/B21/B23/B28/B31/B32/B33/B34/B35）、UI 性能大项（B12 模糊缓存/layout 复用、K21/K23/A15/A7）。

### 批次 9（2026-09-15）：B5、B21、B15、B16、K20（IPC 核心）

- `8083f76` fix(WeaselIPC): log and drop malformed response archives instead of a modal box — B5（P2）。TryDeserialize 改收原始行、归档流/归档构造全部挪进 try，archive_exception 落 LOG(ERROR)（不再在宿主输入线程弹模态框）；_StoreCand 对 candies>100（对齐 UI 侧 MAX_CANDIDATES_COUNT）整体丢弃。TestResponseParser 新增 test_8（损坏/截断归档 + 101 候选用例）；负向验证：还原 MessageBoxA 则测试挂死在模态框（timeout 15s 捕获），去掉上限校验则 test_8 exit 1。
- `a6d9e9b` fix(WeaselIPC): make the deserializer action registry init thread-safe — B21。s_factories 静态成员 + 无锁 empty() 检查 + Define 插入，改为函数内 static const map（C++11 magic statics 恰好一次初始化），删除无人使用的 Define API，注册表内容不变。TestResponseParser 新增 test_9（8 线程起跑线屏障后并发首次构造解析，置于 main 首位保证进程首次构造发生在多线程）；负向验证：还原旧竞态代码 50 次运行 1 次失败（UB 概率性复现），修复后 50/50 通过。
- `8314d82` fix(WeaselIPC): fail explicitly when a request body overflows the send buffer — B16。_Send 检测暂存体 tellp()==-1（wbufferstream failbit）即清暂存、LOG(ERROR) 并抛 ERROR_MORE_DATA，不再静默只发头；顺带删除不可达的 buff_size 钳位。TestPipeChannel 新增超限体用例（必须抛错、零投递、通道可继续用）；负向验证：还原旧行为两条断言 FAIL（静默成功 + 头部已投递）。
- `4d8777f` fix(WeaselIPC): drop staged request bodies on every Transact failure — B15。_Ensure 失败分支抛错前 ClearBufferStream，mid-request catch 在 _Reconnect 前同样清理——任何 Transact 失败后 TSS 通道状态干净，失败请求的暂存体不再拼进下一次 START_SESSION 或搭车无体命令。TestPipeChannel 新增双失败模式用例（服务端不可达 / 连接中途断开），各自重试的 START_SESSION 服务端必须恰好收到一份客户端信息（重试按真实 StartSession 语义逐次重暂存）；负向验证：还原清理逻辑两条"恰好一份"断言 FAIL（双份 body）。
- `63d3cae` fix(WeaselIPC): match the RequestHandler signatures in the test server — K20（P2）。TestRequestHandler 四个方法改为与基类逐字一致的签名（DWORD/EatLine）并加 override（签名漂移变编译错误）；AddSession 经 eat 回调推送状态行。默认（无参）模式改为进程内自测：经 RequestHandler 基类指针调用（与 ServerImpl::OnStartSession 同形），断言会话计数 1→2 增长、FindSession 应答、两个 eat 回调生效——旧默认模式只连当时在跑的服务端、从不触达本 handler（且本机有常驻 WeaselServer.exe，exit 0 依赖环境）。手动 harness 保留为 /start /stop /console /client；负向验证：还原隐藏签名 4 条断言 FAIL、exit 1。
- 构建：release/debug 全量 build ok；9 个测试目标全过（TestPipeChannel 53 用例；TestResponseParser 9 组；TestWeaselIPC 自测 7 断言且不再依赖常驻服务端）。

### 批次 10（2026-09-15）：B6、B34、B28、B31、B35④⑦（管道/基建）

- `3b7f9c9` fix(WeaselIPC): close the thread-local pipe handle when its thread exits — B6（P2）。TSS 从裸 `HANDLE*` 改为 RAII `PipeHandleOwner`（析构执行 _FinalizePipe 语义：DisconnectNamedPipe+CloseHandle，幂等——显式 Disconnect/_Reconnect 先置 INVALID，不会双重关闭）；`_FinalizePipe` 收敛为两者共用的自由函数 `FinalizePipeHandle`（服务端 Listen catch/_ConnectServerPipe catch/_ProcessPipeThread 的裸句柄路径同用它）。核对结论：服务端线程从不触碰 hpipe TSS（Listen/worker 全用局部句柄），泄漏仅在客户端宿主线程；boost TSS 线程退出清理 = delete（实测 boost 1.92 源码确认），包装后即触发析构关句柄。TestPipeChannel 新增 B6 用例（共享通道 + 8 个短命线程连接后不 Disconnect 直接退出）：服务端 worker 注册表必须清空（泄漏句柄会钉死 worker 的 ReadFile）+ 进程句柄数必须回基线；负向验证：还原旧代码两条断言 FAIL（worker 阻塞不清零、句柄数 +8），exit 1。
- `090634b` fix(WeaselIPC): make ClientImpl::session_id an atomic — B34。`std::atomic<UINT>` + 宽松序（纯值读写不携带顺序），全部使用点走 `_SessionId()/_SetSessionId()`；按加固处理（形式 UB、无实证并发路径，x86 对齐 UINT 实际良性）。验证：编译期类型级变更 + 全量构建/测试；无功能级负向验证可做（良性竞态无可观测退化）。
- `37e5eda` fix(WeaselIPCServer): resolve PhysicalToLogicalPointForPerMonitorDPI once, null-check before use — B28。构造函数一次解析为类型化成员指针，OnUpdateInputPosition 判空调用；解析失败跳过转换保持物理坐标（现状为不判空调 null 即崩，判空属防御——WeaselServer.exe 有 IsWindowsBlueOrLaterEx 进程级 gate（WeaselServer.cpp:29），Win8.1+ 该导出必在）。无法单测（依赖 ServerImpl 窗口消息路径），验证方式：全量构建 + 现有套件 + 回退语义核对；负向验证不可复现（本机 Win10 必有该导出）。
- `7aa5fb9` fix(WeaselUtility): check the second GetUserName result in getUsername — B31（+测试修正 `06aefcf`）。第二次 GetUserName 返回值检查，失败走与首次相同的空串路径（旧代码失败时 len 不变照样用未初始化缓冲构造 wstring）。TestResponseParser test_10：返回非空且长度与查询调用精确一致；失败路径无法注入（win32 调用不可 mock，除非重构 API），按检查验证，成功路径负向无退化可断言（还原旧代码 test_10 仍绿）。附注：06aefcf 修正 test_10 误写 `weasel::getUsername()`（该头文件无 weasel 命名空间，此前被 cmd 管道返回 tail 退出码的假象掩盖，未真正编译过）。
- `931afeb` fix(WeaselUtility): decode DebugStream<<std::string as utf-8 like const char* — B35④。与 const char* 分支统一用 u8tow 解码，收参改 const 引用（原 acptow 与注释/兄弟分支矛盾，日志乱码）。TestResponseParser test_11 钉死解码器语义：非 ASCII 的 utf-8 字节 u8tow 还原、acptow 必不还原；DebugStream 输出经 OutputDebugString 不可捕获，分支一致性按代码审阅确认，负向验证同 B31（还原 operator 改动 test_11 仍绿，因其钉的是解码函数而非 operator）。
- `088bf21` fix(WeaselIPC): drop the dead ServerLauncher parameter of Client::Connect — B35⑦。调查结论：管道化 IPC 重写以来该参数一直被忽略（上游 master 同样如此，历史 launcher 语义属管道化之前的 HWND 消息客户端）；仓库内全部调用方只传默认值或 NULL（WeaselTSF 传 NULL，其余不传），测试无人传，死服务端实际全靠 autostart 注册兜底。选择删除参数（编译期暴露漏改调用方，且不引入"连接失败即启动进程"的新风险面），ServerLauncher typedef 一并移除（CommandHandler 另有菜单用途保留）；负向验证：还原 `Connect(NULL)` 调用即编译错误 C2660。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；9 个测试目标全过。TestPipeChannel 现 73 项断言（B6 新增 8 项）；TestResponseParser 增 test_10/test_11。B35 其余子项（①②③⑤⑥）归后续批次，B35 行状态未改。

### 批次 11（2026-09-15）：B22、B32、B33、B35③（RimeWithWeasel）

- `67c1507` fix(RimeWithWeasel): null-check m_ui before the closing SetStyle of UpdateColorTheme — B35③。与同函数上文（config 块内）判空风格一致；触发前提是 m_ui 为空（当前 WeaselServerApp 恒传入 &m_ui，属防御性收口）。验证：全量构建 + 既有套件；无运行时负向验证可做（构造期 ui 恒非空，注入需要重构构造路径，不值得）。
- `b9d89f0` fix(RimeWithWeasel): reject failed rime create_session in AddSession — B32。`create_session()` 返回 0（rime 初始化失败/维护中，librime `RimeCreateSession` → `Service::CreateSession` 在服务未启动时返回 0）时 LOG(ERROR) 并返回 0，不再把 session_id=0 的死会话入表（后续 5+ 次 rime 调用静默失败的"假会话"）。返回值语义核对：客户端 `ClientImpl::StartSession` 将结果存入 session_id，0 即 `_Active()==false`，后续 ProcessKeyEvent/FocusIn 全部直通跳过，TSF 侧重连/重开会话时再次 StartSession 自动重试——客户端天然感知失败，无需额外信令。验证：构建 + 代码路径核对（维护中建会话：AddSession 先走 EndMaintenance 自愈，仍失败即返回 0）；负向验证不可行（create_session 返回 0 需真实 librime 运行态，仓库内 librime/ 为空、仅预编译产物，无法在单测中构造）。
- `418d24a` fix(RimeWithWeasel): never insert into the session map on lookup — B22。`to_session_id/get_session_status`（operator[] 默认插入）改为 find 语义：to_session_id 未知会话返回 0（librime 无效会话号，API 调用安全落空，librime `RimeFindSession`/`RimeGetStatus` 对 session_id=0 显式返回 False）；get_session_status 改为返回指针的 find_session_status，8 个调用点逐一判空早退/降级，new_session_status（AddSession）成为会话表唯一插入点（grep 证明 `m_session_status_map[` 全仓仅此一处）。行为保持：_Respond 对未知会话用本地 orphan 状态应答（响应行形态不变；唯一差异是死条目原本记忆的 __synced 不复存在，未知会话每次重发 style 行——更自洽）；维护路径 `_UpdateUI(0)` 与未知/恶意 ipc_id 不再撑大 map（也消除了 `_GenerateNewWeaselSessionId` 在 map 尾部被 0xFFFFFFFF 级死键占据后自增回绕到 0 的隐患）。有意的行为改进（原为死条目退化路径）：UpdateColorTheme 无活动会话时退回刚刷新的 m_base_style，而非默认构造的裸 UIStyle。验证：TestWeaselIPC 不链接 RimeWithWeasel（deps 仅 WeaselIPC/WeaselIPCServer，服务端走 TestRequestHandler 替身；RimeWithWeaselHandler 构造函数即调 rime_api->setup 写真实用户目录、依赖全局 rime_api 与 weasel::UI，无单测基建），采用全量构建 + 逐调用点核对 + grep 证明唯一插入点；负向验证不可行同 B32（需真实 librime 会话态），非插入路径行为不变性按代码等价性论证（每处早退分支在旧代码中对应的都是"对默认构造条目操作且结果无人消费"）。
- `35b9789` docs(RimeWithWeasel): record the non-reentrancy constraint of m_notifier_mutex — B33，裁决为**不修（有据）**。佐证（librime@33e7814 源码）：OnNotify 持锁调用的 `rime_api->get_state_label` 是纯配置查询——`RimeGetStateLabel` → `RimeGetStateLabelAbbreviated` → `Service::instance().GetSession(session_id)` + `session->schema()->config()` + `Switches::GetStateLabel`，仅读 config 的 switches/states 列表，不派发通知、不回调 handler、不触发部署；真正同步派发通知的路径（set_option 的 option 变更、部署事件）均在锁外调用。故"get_state_label 同步回调 OnNotify 重入自锁"的前提不成立，自锁不可达，锁结构维持非递归即可（改 recursive_mutex 反而是掩盖式修法）。代码注释记录该不可重入约束（持锁段内不得调用会同步触发 OnNotify 的 rime API）。static 评估结论：m_message_*/m_notifier_mutex 保持 static——OnNotify 运行于 rime 线程（含部署线程）且不解引用 this，handler 析构后被 librime 回调亦安全；改为实例成员反而引入悬垂 this 风险，单实例服务进程内 static 共享无实际影响，不值得去 static。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；9 个测试目标全过（两次运行 0 failed）。B35 行状态未改（本批仅完成 ③ 子项，其余子项归后续批次）。

### 批次 12（2026-09-15）：K2、K3、K24、A8（WeaselTSF 静态/线程安全）

- `7e41ec3` fix(WeaselTSF): own the caps-lock simulation state per instance — K2（P2）。文件级三 static（prevKeyEvent/prevfEaten/keyCountToSimulate）改为 WeaselTSF 实例成员（默认值与原 static 初始化等价）：同进程多 TIP 实例（每 thread manager 一个，如多 STA 输入线程宿主）与多线程不再共享 Caps Lock 模拟状态——A 线程按键不再改写 B 线程可见的判定输入。同实例语义核对：键事件由 advise 时所属 thread manager 的 keystroke manager 派发到本线程，down/up 与 SendInput 模拟键（注入到焦点输入队列＝处理原键的同一线程）恒命中同一实例，实例化即线程隔离。附带收口"未写即存"：`_keyCountToSimulate != 0` 期间 `*pfEaten` 原本不写却无条件存入 prevfEaten 并上报 TSF（垃圾值可污染下一轮模拟判定），现先置 FALSE 再按需覆写（模拟键不转发服务器，"未吃"语义不变）。TSF COM 交互无法控制台复现，验证：全量构建 + 路径推演。
- `09b2e4c` fix(WeaselTSF): make ConvertKeyEvent reentrant and pass the real scan code — K3（P2）。ToUnicodeEx 兜底路径的 `static WCHAR buf[8]/static BYTE table[256]` 改栈上局部（并发键事件不再撕裂缓冲；每键 256 字节 memcpy 相对本就存在的 GetKeyboardState+IPC 可忽略）；`ToUnicodeEx(vkey, UINT(kinfo), ...)` 第二参按 MS 文档收 8 位硬件扫描码，改传 `kinfo.scanCode`（原打包值低 16 位是 repeatCount——常态翻译按 VK 进行故侥幸可用，但扫描码真正生效的场合（Alt+小键盘输入、press/release 区分）拿到的是垃圾；bit15 key-up 标记维持不设，与既有行为一致）。上游 master 同位置仍是旧代码（static + 打包值），本仓独立修复。新增 test/TestKeyEvent（debug 段第 10 个测试目标，参考 TestCompartmentUtil 直接编译 WeaselTSF/KeyEvent.cpp）：18 项布局无关断言——KeyInfo 位域解包/`operator UINT32` 打包往返、VK→ibus 翻译（Enter/KP_Enter、Shift_L/R 按扫描码、Control_R 扩展位等）、修饰 mask（Shift/Ctrl/Alt/LOCK、Caps 按下 XOR 还原）、未分配 VK（0xE8）走 ToUnicodeEx 兜底判未知键不崩；字符兜底依赖活动键盘布局（法式 AZERTY 数字行即不同），不做字符级断言，扫描码修正按构造（位域提取）+ 文档契约验证，负向验证不适用（还原旧传参测试仍绿，差异仅在扫描码敏感场合）。
- K24 裁决为**不修（上游一致/防串扰设计）**。考证：① 上游 rime/weasel master 的 KeyEventSink.cpp `OnSetFocus(FALSE)` 与本仓逐字一致（`m_client.FocusOut(); _AbortComposition();`，WebFetch 2026-09-15 核对）；② 本仓该文件仅 3 个 commit（路径/签名修饰/01885fc 响应解析），失焦 abort 非本仓引入；③ 设计上 TSF 组合（composition range）绑定焦点文档，失焦后残留组合在旧应用文档中属非法状态（宿主亦会主动终止触发 OnCompositionTerminated），且不清编码会把 A 应用打到一半的句子串进 B 应用（跨应用串输入状态）。OnCompositionTerminated 中"宿主终止但保留 Rime 编码"的 8f2561f 路径针对同焦点内的空组合终止，与失焦路径语义不同，不构成"失焦可保留"的反例。修改将引入组合状态机跨焦点的不确定性（切回焦点需恢复 TSF 组合+候选窗+Rime 会话三方状态），按风险控制原则不修。
- `37f0da8` refactor(WeaselTSF): remove the dead error_message helper — A8。全仓 grep 零调用（仅报告内提及），连同其"输入线程弹模态框 + static GetTickCount 回绕判定"陷阱形态一并删除；`get_weasel_ime_name` 在 Register.cpp 另有使用，无连带清理。负向验证：删除为纯死代码移除，编译即验证。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 10 个全过（原 9 个 + 新增 TestKeyEvent，run_tests.sh 已同步）。

### 批次 13（2026-09-15）：A5、A6、K5、K22①②③（WeaselTSF P3）

- `8266e0b` fix(WeaselTSF): null-check _pComposition in the empty-range attribute path — A5。`_SetCompositionDisplayAttributes` 的 `pRangeComposition == nullptr` 分支先判 `!_pComposition` 返回 FALSE 再 GetRange（异步会话排队后、执行前 `_FinalizeComposition()` 置空的场景），与同文件 `_ClearCompositionDisplayAttributes` 的判空注释呼应。验证：TSF COM 路径无法控制台复现，构建+推理（当前唯一调用方 Composition.cpp 恒传非空 range，属防御性收口，无行为变化）。
- `7b2b021` fix(WeaselTSF): zero-init the display attribute atom and skip writes when unregistered — A6。`TfGuidAtom _gaDisplayAttributeInput = 0` 就地初始化；`_SetCompositionDisplayAttributes` 开头判 `== 0` 返回 FALSE（不把垃圾 atom 写进 GUID_PROP_ATTRIBUTE）；ActivateEx 调用点注释改写为"初始化失败非致命，atom 保持 0、setter 跳过写入"（失败场景已被 0 守卫兜住，无需 LOG）。验证：构建+推理（0 守卫按任务设计兜底：RegisterGUID 失败时 atom 保持 0、setter 跳过写入；即便极端情况下 0 为合法 atom，跳过的也只是一次属性写入而非写入垃圾值）。
- `20d0f4c` fix(WeaselTSF): clean the real HKLM TIP key on unregister, keep the legacy typo cleanup — K5（P2）。历史考证：该清理源自上游 2013 年 commit 2676cfb（googlecode Issue 531，"remaining registry key for TSF on Windows 8 after install"），意图即删 Win8 残留的**真实** TIP 键 `HKLM\SOFTWARE\Microsoft\CTF\TIP\{clsid}`（本机 reg query 证实该键真实存在），但作者把根键写成 HKCR、拼写误作 "Microsft"，清理对真键从未生效（HKCR 只合并 Software\Classes）。修复：新增 `DeleteTipKeyUnderRoot(root, TipKeyPath, clsid)`（enum class 选择路径拼写，编译期防错；键不存在视为成功）；UnregisterServer 同时清理两个位置——正确键 HKLM\SOFTWARE\Microsoft\CTF\TIP + 历史坏键 HKCR\Software\Microsft\CTF\TIP（沿用错误拼写，本机核实不存在、其他机器可能残留）；返回 BOOL 并核对了 RecurseDeleteKeyA 的删除结果，DllUnregisterServer 失败改报 SELFREG_E_CLASS。新增 test/TestRegisterTipKeys（debug 段第 11 个测试目标，编译 Register.cpp+Globals.cpp+FindIME.cpp）：5 项断言——缺失键算成功、Registered 清理只删 Microsoft 拼写子树（嵌套子键递归删净）且不动 Microsft 树、Legacy 清理删 Microsft 子树；测试用假 CLSID 在 HKCU 造键、删完即清（不触碰本机真实注册项，HKLM 真键仅 reg query 只读核对）；负向验证：把 Registered 前缀临时改回错误拼写，2 条断言 FAIL、exit 1，恢复后全绿。
- `f7cd26f` fix(WeaselTSF): return S_OK from edit sessions on success — K22①。CStartCompositionEditSession::DoEditSession 成功路径返回 S_OK（原 `hr` 初始化 E_FAIL 且成功分支不更新，两处成功路径误报失败），StartComposition 失败/空 composition 仍返回 E_FAIL；WeaselTSF::DoEditSession 末尾 `return TRUE`（=1=S_FALSE）改 S_OK。grep 证明无调用方依赖旧值：全部 RequestEditSession 调用点中仅 `_UpdateCompositionWindow` 用 SUCCEEDED(hr) 且该会话（CGetTextExtentEditSession）本就返回 S_OK；编辑会话返回值语义（同步授予时透传 phrSession）修正为文档要求的 S_OK。验证：构建+推理。
- `abcf981` fix(WeaselTSF): track both compartment advise results, log failures — K22②。`_InitCompartment` 的 `DWORD hr` 改两个独立 HRESULT（hrKeyboard/hrConversion），第一个失败不再被第二个成功覆盖；任一失败 LOG(ERROR)（include/logging.h，默认 no_logging 编译为空、开启 glog 时生效）并返回 FALSE（现状对可检测失败即中止激活，对称化后首个失败同样中止；Deactivate→_Unadvise 判空安全——A4 修复保证了这条路径）。验证：构建+推理。
- `1ddc53d` fix(WeaselTSF): handle ToUnicodeEx surrogate pairs and dead keys in the fallback — K22③。核对 MSDN（ToUnicodeEx，2026-09-15）：返回值 <0 为死键（buf 只是死键字符的 spacing 版本，非本键翻译）、>0 为写入的 UTF-16 码元数、"布局可能以代理对返回增补字符"。修复：`ret==2` 且 buf[0..1] 构成合法代理对时合成完整码位作为 keycode（与单字符路径同为"码位即 keycode"语义）；死键与"死键无法组合返回两个非代理码元"的场合均按未知键处理（ret==-1 原本即落 false 分支，注释写明契约；死键残留双码元场景维持现状不变）。死键/代理对依赖真实键盘布局无法在单测模拟（TestKeyEvent 布局无关约束），验证：文档契约论证 + 构建 + 既有 18 项断言全绿。
- K22 行状态未改：本批完成其 TSF 子集 3 组（Composition/EditSession 返回值、Compartment Advise 覆盖、KeyEvent ToUnicodeEx），族内其余（TextEditSink 清理跳过、KeyEventSink pending 挂起、dllmain 吞异常、WeaselService/WeaselServer/WeaselSetup/WeaselDeployer 各项）归批次 14。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（原 10 个 + 新增 TestRegisterTipKeys，run_tests.sh 已同步）。

### 批次 14（2026-09-15）：K22④⑤⑥⑦⑧⑨（WeaselTSF K22 剩余）

- `1b5b7bd` fix(WeaselTSF): unadvise each text sink cookie by its own validity — K22④。`_InitTextEditSink` 的清理块原以 `_dwTextEditSinkCookie != TF_INVALID_COOKIE` 为总开关：text-edit advise 失败（cookie 置 INVALID）而 layout advise 成功时（fRet=TRUE、context 保留），下轮清理整块跳过，已成功的 layout sink cookie 泄漏（旧 context 上的通知仍投递到本对象）。修复：任一 cookie 有效即进入清理，仅 unadvise 有效者，且两 cookie 一并复位（原代码漏复位 layout cookie）。验证：构建+推理（状态组合枚举：单边成功→必经新条件完成清理；双失败→双 INVALID 且 context 已置空，清理正确跳过；Deactivate 走 `pDocMgr==NULL` 同一清理路径）。TSF COM 无法控制台复现。
- `3514b28` fix(WeaselTSF): reset the key-event pending state per key identity — K22⑤。考证：上游 rime/weasel master 的 KeyEventSink.cpp 与本仓逐字一致（同样布尔 pending，WebFetch 2026-09-15 核对），上游无修复可援引，按保守思路自行设计。缺陷机制：OnTestKeyDown eaten 后置 pending=TRUE，若应用吞掉该键不再回调 OnKeyDown，布尔 pending 永久挂起，后续每个 OnTestKeyDown 都被判重直接 eaten=TRUE、永不到达服务器（直到某次 key-up 才被交叉清除）。修复：pending 状态（down/up 两侧对称，struct PendingTestKey）记录置位键的 (wParam, lParam)，仅同一键事件的重复 test 判重（WORD 2010 单事件多次 OnTestKeyDown 去重保持）；任何不同键事件即开新周期——OnTestKeyDown 处理新键并重置 pending，OnKeyDown/OnKeyUp 对不同键正常 _ProcessKeyEvent。QQ2012（仅 OnKeyDown）、正常 test→key 配对、自动重复（repeat 的 lParam previous-state 位不同且配对 key 已消费 pending）行为均不变；极限场景：应用吞掉配对回调后**同键同 lParam** 再次按下仍会漏送一次（键身份信息量所限，远窄于原"全部后续键被吞"）。验证：构建+推理（全量状态机推演含上述 5 类序列）+ TestKeyEvent 18 项断言全绿（KeyEvent.cpp 未动，纯回归）。
- K22⑥ 裁决**不修（重入环路不可达）**。考证：MSDN《ITfCompartment::SetValue》Return values 明列 E_UNEXPECTED 触发条件 "this method was called during a ITfCompartmentEventSink::OnChange notification"，《ITfCompartmentEventSink::OnChange》Remarks 亦写明 "SetValue will return E_UNEXPECTED if called from within this notification"（2026-09-15 核对，两页互证）。核对当前实现：OPENCLOSE 处理器内唯一的自管写入是 `_SetKeyboardOpen(true)`（Compartment.cpp，写入的正是正在通知的同一 OPENCLOSE compartment），TSF 对该写入直接返回 E_UNEXPECTED、不落值、不再触发 sink——"写触发自己 sink"的重入环路在协议层即被阻断，写前比较现值无从谈起（写本身就不发生）。上游 master 同位置代码逐字一致。附带发现（超出本项，记录备查）：这意味着 `ToggleImeOnOpenClose != yes` 模式下"翻 ascii 后强制重开键盘"的写入恒被 TSF 拒绝且返回值被忽略——属功能性缺陷（键盘实际不会重开）而非重入崩溃，修复需把写延迟到 OnChange 返回之后（投递消息等），留待后续批次。验证：文档契约论证 + 构建。
- `03fc270` fix(WeaselTSF): keep host crash reporting alive in the exception filter — K22⑦。考证：过滤器用途是向 `%TEMP%\rime.weasel` 写 minidump 供小狼毫崩溃诊断（上游 master 同代码同返回值，非本仓引入）；原返回 EXCEPTION_EXECUTE_HANDLER 使**任意宿主进程**异常（含与 weasel 无关的崩溃）被吞——进程静默终止、WER/宿主自身崩溃上报链全部失效。修复（按最小改动）：保留 minidump 记录，返回值改 EXCEPTION_CONTINUE_SEARCH 让宿主/WER 链继续处理。未采用"转发宿主旧过滤器"方案：旧过滤器指针可能随宿主 DLL 卸载而悬空，在崩溃回调里二次崩溃风险更大。验证：构建+推理（按 MSDN SetUnhandledExceptionFilter 返回值语义；过滤器为进程级末端回调，无单测入口）。
- `1c8406b` refactor(WeaselTSF): remove unused GetActiveProfileLangId — K22⑧。git grep 全仓（含 test/）零调用，static 函数纯删除；其 CComPtr/ITfInputProcessorProfileMgr 用法为该文件唯一，无连带清理。验证：构建（编译期证明无引用）。
- `fb689b8` refactor(WeaselTSF): remove unused CCandidateList::UpdateStyle — K22⑨。git grep 全仓（含 test/）零调用，函数+声明删除；style 变更实际经 StartUI 重建 UI 时落入（与审查注记一致），git 历史可找回。验证：构建（编译期证明无引用）。
- K22 的 TSF 部分全部收口（批次 13 ①②③ + 本批 ④-⑨，其中 ⑥ 裁决不修）；§0 行状态改 🔧。族内 Deployer/Setup/Server 侧子项（user_name[20]、Deployer 静默退出、CustomInstall detached 线程、SetEnvironmentVariable throw、WeaselService 死代码等）归批次 16/17/18。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（run_tests.sh）。

### 批次 15（2026-09-15）：A7、K23、K21、A15（WeaselTSF 性能）

主题：击键/焦点热路径性能。热路径定义：每次击键（含纯 ASCII 直通）与每次焦点/线程焦点切换。逐项给出可数指标（COM 调用、IPC 往返、堆分配、读盘）前后对比，行为不变性论证见各 commit message 与下述摘要。

- `d09c18e` fix(WeaselTSF): skip stale conversion-mode compartment write — A7（bug+perf）。bug 部分：`_UpdateLanguageBar` 读取 INPUTMODE_CONVERSION 失败（VT_EMPTY 之外的 FAILED 态）时 flags 保持 0，仅按 ascii/full_shape 重建回写，清掉宿主设置的 TF_CONVERSIONMODE_ROMAN/KATAKANA/NO_CONVERSION 等无关位——改为读取失败不回写（保留 compartment 现值）。S_FALSE（VT_EMPTY，从未设置）仍以 0 为基首次初始化，与原语义一致。perf 部分：缓存上次成功写入值，新值与之相同则跳过 SetValue。读保留（外部改动可见性不劣化：宿主改写的外部位在下一次真需要写时仍被读取并保留；读值无其他消费方，全仓核对过）；Deactivate 时缓存失效。同值 SetValue 本就不触发 OnChange（否则现网早该在 _HandleCompartment↔SetValue 环路上栈溢出），跳过等价。计数：每键 6 次 COM（读 3+写 3）→ 3 次（读 3+写 0）；读失败路径写 1 次 → 0 次。
- `00f7695` perf(WeaselTSF): notify the language bar only on real status change — K23a。`UpdateWeaselStatus` 原无条件 `OnUpdate(TF_LBI_STATUS|TF_LBI_ICON)`，每键触发系统回查 GetStatus/GetIcon。改为 ascii_mode/zhung 图标路径/ascii 图标路径三项比较均无变化才跳过。行为不变性：图标内容由三项惟一决定，无变化时系统重查结果相同；`_status` 位不经本函数变化（SetLangbarStatus 自带变化检测）；OnClick 自身的中/英切换仍直接 OnUpdate。计数：稳态每键 OnUpdate 1 次（连带系统 GetStatus+GetIcon 各 1 次）→ 0 次。
- `a34af93` perf(WeaselTSF): cache file-loaded lang bar icons, hand out copies — K23b。`GetIcon` 自定义图标路径每次 `LR_LOADFROMFILE` 读盘。所有权考证（MSDN《ITfLangBarItemButton::GetIcon》）：调用方（语言栏）负责 `DestroyIcon` 销毁返回值——故不能直接返回缓存句柄（二次销毁/使用已销毁句柄）。方案：按路径缓存 master HICON（本对象持有，析构统一销毁；路径变化即重载，频率≈schema 切换），GetIcon 每次返回 `CopyIcon` 副本。行为不变性：副本与新加载句柄对语言栏等价（同为可销毁独立句柄、像素相同）；加载失败仍 NULL+E_FAIL；资源图标 LR_SHARED 路径原样保留（DestroyIcon 对共享句柄为文档化无效操作，上游既有行为）。计数：每次 GetIcon 磁盘读 1 次+句柄分配 1 → 内存 CopyIcon 1 次、磁盘读 0（路径变化时 1 次）。
- `78d08c1` perf(WeaselTSF): cache the keyboard-disabled state with compartment sinks — K23c。`_IsKeyboardDisabled` 每键 ~7 次 COM（GetFocus+GetTop+QI+2×GetCompartment/GetValue，另有 4 次 Release）。KEYBOARD_DISABLED/EMPTYCONTEXT 是 context 级 compartment：两个 CCompartmentEventSink（复用既有基建）挂在焦点 top context 上，值变化置脏；`_IsKeyboardDisabled` 先 GetFocus/GetTop（2 次 COM），context 身份不变且未脏则返回缓存。context 变化（文档焦点切换、push/pop 改变 GetTop）按身份比对自动重挂+失效；advise 失败或无焦点路径不缓存，退回逐键全量查询（与原实现一致）。行为不变性：缓存仅在 sink 成功挂上当前查询的同一 context 后启用，其两个 compartment 的任何 SetValue 都会置脏（TSF compartment sink 保证，与现有 OPENCLOSE sink 同机制）；无焦点/无 top context 仍返回 TRUE 不缓存。计数：稳态每键 7 次 COM → 2 次；焦点 context 切换额外 2 次 advise（切换级非每键级）。
- `2666c70` perf(WeaselTSF): throttle the per-focus registry read, keep the resync IPC — K21。注册表：`ToggleImeOnOpenClose`（仅 WeaselSetup 写入）改为 TTL 10s 限频缓存，外部改动最迟 TTL 后的下一次焦点切换生效，与原逐次读取一致。IPC 考证后**保留**：① Echo 是会话有效性闸门（服务端 OnEcho=FindSession）——省掉后会话失效时服务端对孤儿会话跑 process_key 并把 m_active_session 指向死 id（RimeWithWeasel.cpp:316 无条件赋值），改变服务端可见时序；② ProcessKeyEvent(0)（keycode=0，librime processor 均不吃，服务端 handled=False）的真实作用是借 `_Respond` 取回全量 status 快照（RimeWithWeasel.cpp:788+ 恒写 status.* 行）——tray 中/英切换（作用于服务端 m_active_session）与 global_ascii_mode 跨会话联动（_Respond 内直接改其他会话 option）都可在本客户端零通知时改变本会话状态，焦点切换时须重新同步 _status/语言栏，省掉会让图标陈旧至下一次击键；上游 master 同构（WebFetch 核对）。计数：每次线程焦点切换注册表读 1 次（open+query+close）→ ≤1 次/10s；IPC 2 次往返保留（有据）。
- `1c3e8ec` perf(WeaselTSF): reuse the response parser across parses — A15。01885fc 后 `_ConsumeResponse` 每键构造 ResponseParser：Initialize 注册 action + ActionLoader 按响应 action 列表逐个注册，每动作=对象+控制块+map 节点 3 次堆分配，典型响应 ~9-15 次/键。改为成员 parser 复用 + `Deserializer::Require` 幂等（已注册直接复用），第二次解析起零分配；目标指针每次解析前重指（各 deserializer 均在 Store 时经 `m_pTarget->p_*` 读目标，无构造期缓存）。**make_shared<Context> 按键新建保留（考证承重）**：CInlinePreeditEditSession 持有旧快照 shared_ptr 副本，异步排队会话若读到复用后的同一分配会看到后续键数据，破坏 01885fc 快照隔离；`weasel::Config` 仅含 bool 无堆分配，local+成功后拷贝的失败语义保留（解析失败 _config/_context 不落地，_status 原地解析为既有行为）。行为不变性：deserializer 无跨解析状态，复用与重建等价；一次性 parser（TestResponseParser 等）表为空不命中幂等分支。计数：每键堆分配 ~9-15 次+Context 1 次 → 0 次+Context 1 次（保留）。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（run_tests.sh）。

### 批次 16（2026-09-15）：K9、K10、K22-Deployer 子集（WeaselDeployer）

主题：WeaselDeployer/DictManagementDialog。K22 的 Deployer 侧三项子项本批完成（①进程单实例静默退出、②levers 模块判空、③restore 后列表不刷新）；K22 剩余 Setup/Server 子项归批次 17/18。

- `dd4b6e9` fix(WeaselDeployer): decode user data sync dir as UTF-8 — K9①。OnBackup 的 `get_user_data_sync_dir`（rime 返回 UTF-8）原经 `MultiByteToWideChar(CP_ACP)` 解码，非 ASCII 用户名/目录即乱码致备份目录定位失败；改 `u8tow`（与批次 7 A16 的 0f1574a 同根同修）。验证：u8tow 解码语义已有持久化测试（TestResponseParser test_11：u8tow 还原 / acptow 乱码），本项为调用点替换，构建+推理。
- `0cd2936` fix(WeaselDeployer): size the list-box text buffer before LB_GETTEXT — K9②。OnBackup 用 `WCHAR dict_name[100]` 收 LB_GETTEXT——LB_GETTEXT 不截断整串拷贝，超长词典名即栈溢出；OnExport/OnImport 的 `WCHAR[MAX_PATH]` 同一缺陷模式。新增 `GetSelectedDictName`（LB_GETTEXTLEN 先查长度 + `std::vector<wchar_t>` 动态缓冲，LB_ERR 返回空串与旧行为一致），三处取词典名统一走它；与 WTL 自带 CString 安全重载（内部同为 GetTextLen+动态缓冲）同型。验证：构建+推理（缓冲处理依赖列表框窗口句柄，无纯逻辑可抽出单测）。
- `b01eeea` fix(WeaselDeployer): pair CoUninitialize only with a real COM init — K10。`OpenFolderAndSelectItem` 的 `CoInitializeEx(0, COINIT_MULTITHREADED)` 在 STA 主线程（_tWinMain 已 CoInitialize）返回 RPC_E_CHANGED_MODE 且不加计数，原无条件 `CoUninitialize` 每次拆一层主线程 COM 计数，多次备份/导出后主线程 COM 被拆毁。改为仅 SUCCEEDED（S_OK/S_FALSE，真正加了计数）才配对 Uninitialize；RPC_E_CHANGED_MODE 时主线程 STA 已可用，SHOpenFolderAndSelectItems 照常执行。同文件 DoFileDialog 的 `CoInitialize(NULL)` 在 STA 返回 S_FALSE 且计数配对，无需改动。验证：构建+推理（COM 初始化计数配对语义，按 MSDN CoInitializeEx 返回值契约）。
- `4f580ee` fix(WeaselDeployer): log the single-instance mutex exits — K22①。进程级单实例互斥命中（创建失败/已存在）原先静默 ret=1 退出，无任何痕迹。两处各记一条日志（include/logging.h，WeaselDeployer/stdafx.h 已引入，默认 no_logging 编译为空、开启 glog 生效），措辞与 Configurator.cpp 既有日志一致。保守不弹 MessageBox：部署器会被脚本/计划任务静默调用（/deploy、/sync），弹框会阻塞无人值守流程。验证：构建+推理。
- `a7b8061` fix(WeaselDeployer): survive a missing rime levers module — K22②。构造函数原先直接解引用 `find_module("levers")->get_api()`，levers 模块缺失（部署损坏）即空指针崩溃。改为判空：缺失时 LOG(ERROR) 并留空 api_，OnInitDialog 检测后以新增字符串资源 IDS_STR_ERR_LEVERS_MISSING（159，zh-CN/zh-TW/en 三语言块）提示重新安装/重新部署并 EndDialog 关闭，不崩；对话框随即关闭，后续 api_ 调用不可达。验证：构建（含 .rc 资源编译，UTF-16 编码经 iconv diff 验证仅增三行）+推理。
- `e46664b` fix(WeaselDeployer): reload the dict list after a restore — K22③。restore 成功后 `user_dict_list_` 不刷新，列表仍显示旧状态、后续继续操作旧条目。`Populate()` 改为自包含刷新（ResetContent 后重枚举；程序化 `SetCurSel(-1)` 不触发 LBN_SELCHANGE，末尾手动复位 backup/export/import 按钮与初始化状态一致），OnRestore 成功分支调用 Populate。OnInitDialog 首次填充路径控件已 attach，行为不变。验证：构建+推理。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（run_tests.sh）。

### 批次 17（2026-09-15）：K13、K22-Setup 子集（WeaselSetup）

主题：WeaselSetup 安装器。K13 整项 + K22 的 Setup 侧三项子集本批完成（①SetEnvironmentVariable throw、②WER dump 配置、③修改安装 detached 线程）；K22 仅剩 Server 子项归批次 18。

- `d5c6941` fix(WeaselSetup): recursively delete registry trees and clear HKCU config on uninstall — K13。uninstall() 原用 `RegDeleteKey(HKLM, WEASEL_REG_KEY/RIME_REG_KEY)`，键下有子键（RIME_REG_KEY 必含 Weasel 子键）即失败，卸载从不真正清掉 HKLM 注册信息。①改 `delete_reg_tree`（RegDeleteTreeW 封装，SetupUtil 供测试复用；advapi32 已链接，无需 shlwapi）递归整树删除并检查返回值（ERROR_FILE_NOT_FOUND 视为已清除，其余计入 retval 报卸载失败）。②HKCU 考证：output/install.nsi Uninstall 段只清 HKLM（`DeleteRegKey HKLM SOFTWARE\Rime` 等），全程不碰 HKCU——无人处理，按保守方案在 uninstall() 顺带递归清除 HKCU `Software\Rime\Weasel` 配置键（经 per_user_root()/per_user_subkey() 重定向感知真实用户 hive，覆盖 Profile/Hant/RimeUserDir/Language/Updates 等），用户数据目录文件留给用户处理。验证：TestWeaselSetup 新增嵌套键树用例（两层子键+值整树删除、删后无残留、缺失键返回 ERROR_FILE_NOT_FOUND），构建+运行全绿；HKLM/HKCU 真实删除路径构建+推理（不碰本机真实键）。
- `c4d34d7` fix(WeaselSetup): report and fail instead of throwing on SetEnvironmentVariable error — K22①。register_text_service 里 SetEnvironmentVariable(TEXTSERVICE_PROFILE) 失败原抛 runtime_error，调用链（install_/uninstall_ime_file → install/uninstall → _tWinMain）无人捕获即 std::terminate。改为按本函数既有错误惯例：新增字符串资源 IDS_STR_ERR_SETENV_PROFILE（151，zh-CN/zh-TW/en 三语言块）非静默提示，返回 1 走安装失败路径，不启动 regsvr32。验证：x86 debug 构建（含 .rc 资源编译，UTF-16 经 iconv/perl 字节级 round-trip 校验仅增三行、行尾纯 CRLF）+推理。
- `dffddf2` fix(WeaselSetup): collect a meaningful WER mini dump for WeaselServer — K22②。install() 原写 DumpType=0(custom)+CustomDumpFlags=0，按 MSDN《Collecting User-Mode Dumps》表语义仅产出 MiniDumpNormal 最简内容（0+0 与注释"CustomDumpFlags, MiniDumpNormal"自洽但意图存疑）。考证上游 rime/weasel master 同段逐字同值（WebFetch 2026-09-15 核对），属上游遗留，本仓修为有意义配置：DumpType=1（mini dump，含线程栈与模块信息，足以符号化崩溃栈），删 CustomDumpFlags 写入（该值仅在 DumpType=0 时生效，升级安装残留旧值失效无害）。验证：构建+注册表值语义推理（MSDN：0=custom/1=mini/2=full，CustomDumpFlags 取 MINIDUMP_TYPE 位掩码）。
- `1c14073` fix(WeaselSetup): join the service-restart sequence before exiting — K22③。CustomInstall 修改安装后起 detached 线程做 Sleep(500)+停服务/重启/部署序列，主流程弹完"修改成功"框即返回、_tWinMain 退出，进程结束腰斩线程（服务停在半重启状态）。改为 joinable 线程：MessageBox 移到 join 前（用户读提示的 1 秒里序列并发执行，UX 不变），框关后 join 序列必然完成；用户秒关框最多多等序列剩余约 1 秒。未采用"移到消息循环后同步执行"方案：_tWinMain 无独立消息循环（模态对话框自带），改动面更大。验证：构建+推理（消息框与线程并发的时序枚举）。
- 构建：x86 debug WeaselSetup build ok（本批全部改动均在 x86-only 目标内）；x64 debug 全量 build ok + 测试目标 11 个全过（run_tests.sh）；release 构建通过后已切回 debug。

### 批次 18（2026-09-15）：A11、A12、K22-Server 子集（WeaselServer/托盘）

主题：WeaselServer/SystemTray。A11、A12 整项 + K22 的 Server 侧四项子集本批完成（①user_name[20]、②WeaselService boost::thread 临时对象、③Shutdown()/_stoppedEvent、④TestWeaselIPC /console 不可达 return）；K22 杂项族至此全部收口。

- `49406da` fix(WeaselServer): bound SetIconList loop within the icon array — A11。`SetIconList(HICON*, UINT)` 循环条件 `i <= nNumIcons` 读 `pHIconList[nNumIcons]` 差一越界（全仓无调用者的潜在缺陷），改 `< nNumIcons`。核对另一重载 `SetIconList(UINT, UINT)`：按资源 ID 从 uFirstIconID 到 uLastIconID 逐个 LoadIcon（同样无调用者），闭区间 ID 枚举语义自洽无同病，不动。验证：构建+推理（纯循环边界，无调用者不可运行时测）。
- `9d16c70` fix(WeaselServer): destroy the menu on the early return, drop the duplicate submenu destroy — A12。①OnTrayNotification 双击分支 `if (!hSubMenu) return 0;` 补 `::DestroyMenu(hMenu)`（对照单击分支既有写法与注释）；②SetMenuDefaultItem 删 `::DestroyMenu(hSubMenu)`——MSDN《DestroyMenu》明示父菜单销毁时递归销毁全部子菜单，先销毁子菜单再销毁父菜单构成对同一子菜单二次销毁，保留父菜单销毁即可。修改保持 Chris Maunder 原代码风格。验证：构建+推理（按 MSDN DestroyMenu 递归销毁契约；托盘菜单交互无自动化测试入口）。
- `d386aab` fix(WeaselServer): use getUsername for the SYSTEM account gate — K22①。`WCHAR user_name[20]` 手写 GetUserName 不查返回值：超长用户名（>19 字符）读取失败、缓冲保持全零，`_wcsicmp(L"SYSTEM")` 不中即静默放行服务进程。改用 WeaselUtility.h 的 getUsername()（B31 已修失败路径，动态长度），读取失败返回空串同样不等于 SYSTEM；保持大小写不敏感比较。WeaselUtility.h 原已 include，零新增耦合。验证：构建+推理；getUsername 本体有 TestResponseParser test_10 持久化覆盖（B31），本项为调用点替换。
- `0bd1141` fix(WeaselServer): remove the dead WeaselService SCM harness — K22②③。考证（②③一并裁决）：全仓 git grep 无任何 WeaselService 构造/Run() 调用（WeaselServer.cpp 仅 include 头文件；WEASEL_SERVICE_NAME "WeaselInputService" 除该类自身外零引用，安装器/install.nsi 均不注册该服务）；WeaselServer.exe 的 _tWinMain 从不调用 StartServiceCtrlDispatcher，即使外部把 exe 注册为 Windows 服务，SCM 也无法进入 ServiceMain——整类运行时不可达，属半成品死代码。上游 rime/weasel master 同样仅 include 不使用（WebFetch 2026-09-15 核对），为上游遗留残骸。按"删除死部分"处理：删 WeaselService.cpp/.h（-213 行），WeaselServer.cpp 的 include 换为直接 include WeaselServerApp.h（原经 WeaselService.h 传递获得，App 头自包含无循环），vcxproj/filters 同步移除条目，xmake `add_files("./*.cpp")` 自动收敛。②的 `boost::thread{...}` 临时对象与③的 Shutdown() 报 STOPPED 不停 app、_stoppedEvent 创建即弃均随整类消失。附带考证备注：boost::thread 析构语义为 detach（区别于 std::thread 的 terminate），原"析构即 terminate"表述不准，但代码不可达的死代码结论不受影响。验证：构建（release+debug，链接期证明无残余引用）+ git grep 全仓零引用。
- K22④ 裁决**不修（随批次 9 重写消失）**。原 test/TestWeaselIPC.cpp:36-39 /console 分支 `return 0;` 不可达：批次 9（K20）重写后 _tmain 的 /console 分支为 `return console_main();`（可达），console_main 尾部 `return 0;` 在读循环正常退出后执行——原缺陷形态已不存在，无改动。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（run_tests.sh）。

### 批次 19（2026-09-15）：B23、B35①②⑤⑥、B24 结案（WeaselUI P3 族）

主题：WeaselUI 布局/面板 P3 族。B23 整项 + B35 的 ①②⑤⑥ 四个子项本批完成；B35 杂项族至此全部收口（③ 批次11、④⑦ 批次10）。B24 结案（无代码改动）。

- `f96f9f2` fix(WeaselUI): bound label formatting and validate user format strings — B23。`swprintf_s<128>` 格式化候选 label：超长（140 字符参数，repro_b23 实测退出码 3）触发 CRT invalid-parameter 直接终止进程；且 `label_text_format` 直取用户 yaml（style/label_format），非常规格式符（%d/%n/%ls、尾部孤立 %、第二个 %s）与唯一字符串实参不匹配即 UB。修复：WeaselUtility.h 新增两个内联纯函数——`IsSafeLabelTextFormat`（校验：至多一个 %s、任意个 %%、其余说明符一律拒绝）与 `FormatLabelText`（`_snwprintf_s` + `_TRUNCATE` 有界格式化：超长按 127 wchar 截断返回 -1 不触发 invalid-parameter；非法格式回退 UIStyle 默认 `%s.`），三个调用点（StandardLayout::GetLabelText、RimeWithWeasel `_GetLabelText` 与 PREVIEW_ALL 块）统一走它。验证：TestWeaselUI 新增 test_label_text_format_bounded（13 项断言：140 字符截断不终止、各非法格式回退、%s/%%/空格式正常）并装 `_set_invalid_parameter_handler` 计数器（_TRUNCATE 路径根本不触发 handler，计数器兼作回归哨兵）；负向验证：本地临时还原 swprintf_s 实现，5 项断言 FAIL（handler 拦住终止，进程存活可报告失败），恢复后全绿。
- `31ce111` fix(WeaselUI): bound the neighbor row/col lookup in round-info passes — B35①。HorizontalLayout 与 VHorizontalLayout::DoLayoutWithWrap 两个方向的圆角调整循环在 i==candidates_count-1 时读 `row_of_candidate[i+1]`/`col_of_candidate[i+1]`：count==MAX_CANDIDATES_COUNT(100) 时读下标 [100]，越过 `int[100]` 数组末尾（栈上裸数组，debug CRT 不插桩，运行时无告警）。修复：邻居查询加 `i + 1 < candidates_count` 守卫（最后一项受影响的圆角位已由循环上方显式赋值）。验证：TestWeaselUI 新增 test_max_candidates_round_info——恰 100 候选走横排窄 max_width 多行换行、竖排文本自动换列两个方向，三条布局+绘制全链路 settle 断言全过；负向验证按 _countof 心算说明（数组容量 100，未加守卫时 i==99 求值 [100]，裸数组无法运行期捕获）。
- `86ecf2d` fix(WeaselUI): zero-init the mark-size CSize in all layout DoLayouts — B35②。三个布局（含 VHorizontalLayout::DoLayoutWithWrap 共 4 处）`CSize sg;` 未初始化，candidates_count==0 时跳过测量但仍读 `sg.cx/cy` 进 mark_width/mark_height（UB-but-benign，读值只喂给零候选时用不到的 base_offset）。统一 `CSize sg{0, 0};`。验证：构建 + 全量 TestWeaselUI 绿；count==0 路径读值经检查无下游消费，无可观察行为变化。
- `ac3f9f7` fix(WeaselUI): keep surrogate pairs whole when abbreviating candidates — B35⑤。`_ApplyUpdate` 的候选缩写按 wchar 截断：截断点落在高/低代理中间产生孤立代理（emoji 变 U+FFFD），尾部固定取末一个 wchar 又切开末尾增补平面字符。WeaselUtility.h 新增 `AbbreviateText`：截断点拆对时退一个 wchar（缩写短一位但码点完整），末尾为低代理时连同高代理一起取；BMP 输入与旧表达式逐字节一致。验证：TestWeaselUI 新增 test_candidate_abbreviation_surrogate_safe（5 项断言，U+1F600 双向拆对场景）；负向验证：本地临时还原旧逐 wchar 截断，恰好两条代理对断言 FAIL、三条 BMP 断言仍绿，恢复后全绿。
- `1fe6fc6` fix(WeaselUI): retry custom status icon loads instead of caching failures — B35⑥。`LoadIconNecessary` 在加载前就缓存路径：文件暂缺/损坏时 LoadImage 失败被缓存到路径变化为止，用户中途补装图标文件也一直不显示（且 CIcon 被 NULL 化成空图标）。修复：仅成功才缓存路径，失败保留当前图标、下次绘制自动重试；调用频率已核对——该函数在 DoPaint 内（状态图标可见时每次绘制刷新走到，随用户输入触发而非动画循环），失败重试是一次落空的文件打开，开销可忽略。模板参数换成具体类型（编译期类型检查），空路径回退内置资源图标的行为保留。验证：TestWeaselUI 新增 test_missing_status_icon_files（四个自定义图标路径全缺失 + ascii 模式显示状态图标，两次布局+绘制 settle）；"文件补上后恢复显示"不可经公共 API 观察（CIcon 状态在 panel 内部），按代码审阅确认 loaded_path 仅在加载成功时赋值。
- B24 结案（无代码改动）。批次 3 的 `2ee9b36` 把 DoPaint 首行的 `ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED)` 改为首帧一次性，后被 `0da2dea` 回退：panel 对象可复用于重建的 HWND（UI::Create 复用对象重建窗口等路径），沿用"已切换"标志会让新窗口缺 WS_EX_LAYERED，UpdateLayeredWindow 失败且宿主（如 Electron）吞掉 WM_PAINT 时候选框全黑。样式切换必须每帧执行；ModifyStyleEx 内部先比较再写入，稳态每帧只多一次 GetWindowLong。现实现（WeaselPanel.cpp DoPaint 首行）已带完整注释，保留每帧实现，本项按"结案"处理。
- 构建：release/debug 全量 build ok（release 后已切回 debug）；测试目标 11 个全过（run_tests.sh）。TestWeaselUI 现 45 项断言（批次 19 新增 23 项）。
