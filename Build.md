## 编译步骤 ##
build.bat weasel
# 获取rime.dll
pwsh -File .\get-rime.ps1 -tag latest -use dev
# 打包安装包
powershell -ExecutionPolicy Bypass -File .\make-installer.ps1 -Release

