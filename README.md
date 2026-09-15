> 本仓库是 [rime/weasel](https://github.com/rime/weasel) 的 fork。
>
> 与上游的差异集中在质量加固：2026-09 对全部自研模块（WeaselTSF / WeaselServer / WeaselSetup / WeaselDeployer / WeaselIPC / WeaselIPCServer / WeaselUI / RimeWithWeasel / include 共享头文件，约数万行）做了一轮深度静态代码审查，随后按 21 个修复批次逐项修复、验证并记录。审查报告：[weasel_CODE_REVIEW_20260906.md](weasel_CODE_REVIEW_20260906.md)、[weasel_CODE_REVIEW_20260914.md](weasel_CODE_REVIEW_20260914.md)（含逐项验证结论与批次修复记录）。
>
> AI 声明：上述代码审查、修复实施、测试编写与验证均由 AI 辅助完成。
>
> ### 修复概览
>
> - 规模：登记 bug 67 项 + 性能项 10 项（含 6 项运行时复现确认）；终态全覆盖——已修复 / 不修有据（与上游一致的设计或协议层不可达）/ 经运行时验证推翻无需修复。
> - P1 级：UI 跨线程窗口调用与服务消息线程互等死锁（全系统输入冻结）；用户资料同步失败后服务端永久停留在维护态（现存会话全部禁输且不自愈）。
> - 稳定性：合成期每击键泄漏 ITfRange 的 COM 引用泄漏；托盘快照跨线程读写 `std::wstring` 等数据竞争；反序列化异常在宿主输入线程弹模态框；HR() 异常贯穿 WNDPROC 致服务整体退出；`swprintf_s` 非法参数直接终止进程；多处越界索引 / 未初始化成员 / 空指针解引用收口。
> - 安装 / 部署：WOW64 重定向在提前返回路径不恢复、注册表清理对真实 TIP 键无效、regsvr32 退出码不检查、卸载不清注册表树与 HKCU 配置等安装器缺陷修复。
> - 性能（击键热路径）：直通键早退并复用 rime status 快照；候选向量去重比较提速 23.5×；escape/unescape 提速 8.7×；语言栏 / compartment / 图标按变化缓存（稳态每键 OnUpdate 与图标读盘归零、`_IsKeyboardDisabled` 每键 7→2 次 COM）；高亮阴影模糊位图缓存（内容不变帧零分配零模糊）；响应解析器复用（每键堆分配 ~9-15 次→0）。
> - 测试：测试目标由 5 个增至 11 个，关键修复带负向验证（临时还原缺陷确认测试能捕获）。
>
> 以下为上游 README 原文。

【小狼毫】輸入法
================

基於 中州韻輸入法引擎／Rime Input Method Engine 等開源技術

式恕堂 版權所無

[![Download](https://img.shields.io/github/v/release/rime/weasel)](https://github.com/rime/weasel/releases/latest)
[![Build status](https://github.com/rime/weasel/actions/workflows/commit-ci.yml/badge.svg)](https://github.com/rime/weasel/actions/workflows/commit-ci.yml)
[![GitHub Tag](https://img.shields.io/github/tag/rime/weasel.svg)](https://github.com/rime/weasel)

授權條款：GPLv3

項目主頁：https://rime.im

您可能還需要 RIME 用於其他操作系統的發行版：

  * ibus-rime、fcitx5-rime 或 fcitx-rime 用於 Linux
  * 【鼠鬚管】用於 macOS （64位）

安裝輸入法
----------

本品適用於 Windows 8.1 ~ Windows 11

初次安裝時，安裝程序將顯示「安裝選項」對話框。

若要將【小狼毫】註冊到繁體中文（臺灣）鍵盤佈局，請在「輸入語言」欄選擇「中文（臺灣）」，再點擊「安裝」按鈕。

安裝完成後，仍可由開始菜單打開「安裝選項」更改輸入語言。

使用輸入法
----------

選取輸入法指示器菜單裏的【中】字樣圖標，開始用小狼毫寫字。

可通過快捷鍵 <kbd>Ctrl+`</kbd> 或 <kbd>F4</kbd> 呼出方案選單、切換輸入方式。

定製輸入法
----------

通過 開始菜單 » 小狼毫輸入法 訪問設定工具及常用位置。

用戶詞庫、配置文件位於 `%AppData%\Rime`，可通過菜單中的「用戶文件夾」打開。高水平玩家調教 Rime 輸入法常會用到。

修改詞庫、配置文件後，須「重新部署」方可生效。

定製 Rime 的方法，請參考 Wiki [《定製指南》](https://github.com/rime/home/wiki/CustomizationGuide)。如需定製 Weasel 獨有的樣式和行為，請參考本倉庫 [Wiki 頁面](https://github.com/rime/weasel/wiki)。

致謝
----

### 輸入方案設計：

  * 【朙月拼音】系列及【八股文】詞典
    - 部分數據來源於 CC-CEDICT、Android 拼音、新酷音、opencc 等開源項目
    - 維護者：佛振、瑾昀
  * 【注音／地球拼音】
    - 維護者：佛振、瑾昀
  * 【倉頡五代】
    - 發明人：朱邦復先生
    - 碼表源自 www.chinesecj.com
    - 構詞碼表作者：惜緣

  【五笔】【粵拼】【上海／蘇州吳語】【中古漢語拼音】【國際音標】等衆多方案
  不再以安裝包預裝形式提供。可由 <https://github.com/rime/plum> 下載安裝。

### 程序設計：

  * [佛振](https://github.com/lotem)
  * [鄒旭](https://github.com/zouxu09)
  * [Xiangyan Sun](https://github.com/wishstudio)
  * [Prcuvu](https://github.com/Prcuvu)
  * [nameoverflow](https://github.com/nameoverflow)
  * [fxliang](https://github.com/fxliang)
  * [Azuk 443](https://github.com/determ1ne)

  查看更多 [代碼貢獻者](https://github.com/rime/weasel/graphs/contributors)

### 美術：

  * 圖標設計／[Patricivs](https://github.com/Patricivs)
  * 配色方案／Aben、P1461、Patricivs、skoj、佛振、五磅兔

### 本品引用了以下開源軟件：

  * [Boost C++ Libraries](http://www.boost.org/) (Boost Software License)
  * [curl](https://curl.haxx.se/) (MIT/X derivate license)
  * [google-glog](https://github.com/google/glog) (BSD 3-Clause License)
  * [Google Test](https://github.com/google/googletest) (BSD 3-Clause License)
  * [LevelDB](https://github.com/google/leveldb) (BSD 3-Clause License)
  * [librime](https://github.com/rime/librime) (BSD 3-Clause License)
  * [marisa-trie](https://github.com/s-yata/marisa-trie) (BSD 2-Clause License, LGPL 2.1)
  * [OpenCC / 開放中文轉換](https://github.com/BYVoid/OpenCC) (Apache License 2.0)
  * [plum](https://github.com/rime/plum) (GNU Lesser General Public License v3.0)
  * [WinSparkle](https://github.com/vslavik/winsparkle) (MIT License)
  * [yaml-cpp](https://github.com/jbeder/yaml-cpp) (MIT License)
  * [7-Zip](https://www.7-zip.org) (GNU LGPLv2.1+ with unRAR restriction)

問題與反饋
----------

發現程序有 bug，請到 GitHub 反饋
<https://github.com/rime/weasel/issues>

歡迎提交 pull request
<https://github.com/rime/weasel/pulls>

Rime 輸入法（不限於 Windows 平臺）功能、使用方法與配置相關的問題，請反饋到
<https://github.com/rime/home/issues>

聯繫方式
--------

技術交流，歡迎光臨 [Rime 代碼之家](https://github.com/rime/home)，或致信 Rime 開發者 <rimeime@gmail.com>

謝謝！
