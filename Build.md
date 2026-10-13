## 编译步骤 ##
build.bat weasel
# 获取rime.dll
pwsh -File .\get-rime.ps1 -tag latest -use dev
# 打包安装包
powershell -ExecutionPolicy Bypass -File .\make-installer.ps1 -Release

`make-installer.ps1 -Build` 只编译 x64/x86。若 `output` 中缺少
`weaselARM.dll`、`weaselARM64.dll`、`weaselARM64X.dll` 中的任意一个，生成的
安装包只支持 x86/x64 Windows。它会在 ARM64 上、卸载旧版或复制文件之前退出；
静默安装返回错误码 `1633`，现有安装保持不变。

需要 ARM64 安装包时，先准备 VS 的 ARM、ARM64、ARM64EC 工具链以及对应架构的
Boost 库，在 Developer Command Prompt 中运行 `xbuild.bat arm64`，再运行
`make-installer.ps1`。三个 DLL 齐全时，NSIS 自动打包完整 ARM 组件并允许 ARM64
安装。编译安装包时会输出当前支持的平台。
