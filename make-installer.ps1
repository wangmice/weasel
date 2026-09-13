# make-installer.ps1 - 一键打 NSIS 安装包，独立于 xbuild.bat，无需 Developer Command Prompt
#
# 流程: 计算版本号 -> [可选 -Build: xmake 编译 x64+x86] -> 补齐 output 所需数据 -> 定位/安装 NSIS -> makensis
#
# 用法:
#   powershell -ExecutionPolicy Bypass -File .\make-installer.ps1            # 仅打包（要求 output 下已有编译产物）
#   powershell -ExecutionPolicy Bypass -File .\make-installer.ps1 -Build     # 先 xmake 编译 x64+x86 再打包
#   powershell -ExecutionPolicy Bypass -File .\make-installer.ps1 -Release   # 正式发布版本号 (WEASEL_BUILD=0, 不带 git hash)
#
# 版本号可用环境变量覆盖: VERSION_MAJOR / VERSION_MINOR / VERSION_PATCH / WEASEL_BUILD / RELEASE_BUILD
# 默认从最近 git tag 推算 WEASEL_BUILD 与 git 短 hash，与 xbuild.bat 的逻辑一致
#
# 前置:
#   编译环境 (-Build 需要): 见 setup-build-env.ps1 (boost / librime / xmake)
#   NSIS 缺失时自动安装: 优先 scoop install nsis，否则回退 install_nsis.bat
#   plum 数据拉取需要 bash (Git for Windows) 和网络
#
# 产物: output\archives\weasel-<PRODUCT_VERSION>-installer.exe
# 不含 ARM64 专属 DLL（install.nsi 中为 /nonfatal，ARM64 机器上仍可用 x64 二进制运行）

param(
  [switch]$Build,
  [switch]$Release
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (-not $repo) { $repo = Split-Path -Parent $MyInvocation.MyCommand.Path }
Set-Location $repo

function Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Die($msg) { throw $msg }
function NeedCommand($name, $hint) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) { Die "未找到 $name；$hint" }
}

# ---------------------------------------------------------------------------
Step "1/5 计算版本号"
$major = if ($env:VERSION_MAJOR) { $env:VERSION_MAJOR } else { '0' }
$minor = if ($env:VERSION_MINOR) { $env:VERSION_MINOR } else { '17' }
$patch = if ($env:VERSION_PATCH) { $env:VERSION_PATCH } else { '4' }
$weaselVersion = "$major.$minor.$patch"
$releaseBuild = $Release -or ($env:RELEASE_BUILD -eq '1')

NeedCommand 'git' '打包前需要 git 计算版本号'
$weaselBuild = $env:WEASEL_BUILD
if (-not $weaselBuild) {
  if ($releaseBuild) {
    $weaselBuild = '0'
  } else {
    # 与 xbuild.bat 一致：取 creatordate 倒序第一个包含版本号的 tag
    $lastTag = (& git tag --sort=-creatordate) | Where-Object { $_ -like "*$weaselVersion*" } | Select-Object -First 1
    if ($lastTag) {
      $weaselBuild = (& git rev-list "$lastTag..HEAD" --count)
    } else {
      Write-Host "  [warn] 未找到匹配 $weaselVersion 的 tag，WEASEL_BUILD 记为 0"
      $weaselBuild = '0'
    }
  }
}
$shortHash = (& git rev-parse --short HEAD)
if ($releaseBuild) {
  $productVersion = "$weaselVersion.$weaselBuild"
} else {
  $productVersion = "$weaselVersion.$weaselBuild.$shortHash"
}
$fileVersion = "$weaselVersion.$weaselBuild"
Write-Host "  WEASEL_VERSION  = $weaselVersion"
Write-Host "  WEASEL_BUILD    = $weaselBuild"
Write-Host "  PRODUCT_VERSION = $productVersion"
Write-Host "  FILE_VERSION    = $fileVersion"

# ---------------------------------------------------------------------------
Step "2/5 编译 (可选)"
if (-not $Build) {
  Write-Host "  [skip] 未指定 -Build，使用 output 下现有二进制"
} else {
  NeedCommand 'xmake' '编译需要 xmake，可运行 setup-build-env.ps1 安装'
  if (-not $env:BOOST_ROOT) {
    # 无 env.bat 时 xmake.lua 会回退到 scoop boost；这里仅在 env.bat 存在时读取
    $envBat = Join-Path $repo 'env.bat'
    if (Test-Path $envBat) {
      Get-Content $envBat | ForEach-Object {
        if ($_ -match '^set\s+BOOST_ROOT=(.+)$') { $env:BOOST_ROOT = $Matches[1].Trim() }
      }
    }
  }
  # 避免文件被运行中的服务占用
  if (Test-Path (Join-Path $repo 'output\weaselserver.exe')) {
    & (Join-Path $repo 'output\weaselserver.exe') /q | Out-Null
  }
  $env:VERSION_MAJOR = $major
  $env:VERSION_MINOR = $minor
  $env:VERSION_PATCH = $patch
  $env:FILE_VERSION = $fileVersion
  $env:PRODUCT_VERSION = $productVersion
  foreach ($arch in @('x64', 'x86')) {
    Write-Host "  xmake $arch ..."
    xmake f -a $arch -m release -y
    if ($LASTEXITCODE -ne 0) { Die "xmake f -a $arch 失败" }
    xmake -y
    if ($LASTEXITCODE -ne 0) { Die "xmake -a $arch 编译失败" }
  }
}

# ---------------------------------------------------------------------------
Step "3/5 补齐 output 所需文件"
# install.nsi 中非 /nonfatal 的 File 指令所要求的二进制
$required = @(
  'output\weasel.dll', 'output\weaselx64.dll',
  'output\WeaselDeployer.exe', 'output\WeaselServer.exe', 'output\WeaselSetup.exe',
  'output\rime.dll', 'output\WinSparkle.dll',
  'output\Win32\WeaselDeployer.exe', 'output\Win32\WeaselServer.exe',
  'output\Win32\rime.dll', 'output\Win32\WinSparkle.dll',
  'output\data\weasel.yaml'
)
$missing = @($required | Where-Object { -not (Test-Path (Join-Path $repo $_)) })
if ($missing) {
  Die (@"
缺少以下编译产物:
  $($missing -join "`n  ")
处理方式: 加 -Build 参数让本脚本编译，或先运行 xmake f -a x64 -m release -y; xmake（x86 同理）
rime.dll / Win32\rime.dll 缺失时另需: pwsh -File .\get-rime.ps1 -use dev
"@)
}

# plum 子模块与 LICENSE / README / rime-install.bat
if (-not (Test-Path (Join-Path $repo 'plum\rime-install.bat'))) {
  & git submodule update --init plum
  if ($LASTEXITCODE -ne 0) { Die 'plum 子模块初始化失败' }
}
Copy-Item (Join-Path $repo 'LICENSE.txt') (Join-Path $repo 'output\') -Force
Copy-Item (Join-Path $repo 'README.md') (Join-Path $repo 'output\README.txt') -Force
Copy-Item (Join-Path $repo 'plum\rime-install.bat') (Join-Path $repo 'output\') -Force

# rime 基础数据（default.yaml / essay.txt 等），已存在则跳过
if ((Test-Path (Join-Path $repo 'output\data\essay.txt')) -and (Test-Path (Join-Path $repo 'output\data\default.yaml'))) {
  Write-Host "  [skip] rime 基础数据已存在 (output\data)"
} else {
  NeedCommand 'bash' '拉取 rime 数据需要 bash (Git for Windows)'
  $env:plum_dir = 'plum'
  $env:rime_dir = 'output/data'
  $env:WSLENV = 'plum_dir:rime_dir'
  try {
    bash plum/rime-install rime-prelude rime-essay
    if ($LASTEXITCODE -ne 0) { Die 'plum 拉取 rime 数据失败' }
  } finally {
    Remove-Item env:plum_dir, env:rime_dir, env:WSLENV -ErrorAction SilentlyContinue
  }
}

# opencc 数据来自 librime 预编译包，缺失时回退到 get-rime.ps1
if (-not (Test-Path (Join-Path $repo 'output\data\opencc\TSCharacters.ocd2'))) {
  NeedCommand 'pwsh' '补齐 opencc 数据需要 pwsh 运行 get-rime.ps1'
  pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'get-rime.ps1') -use dev
  if ($LASTEXITCODE -ne 0) { Die 'get-rime.ps1 -use dev 失败（opencc 数据缺失）' }
}

# ---------------------------------------------------------------------------
Step "4/5 定位 NSIS (makensis)"
function Find-Makensis {
  $cmd = Get-Command makensis -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
    if ($base -and (Test-Path (Join-Path $base 'NSIS\Bin\makensis.exe'))) {
      return (Join-Path $base 'NSIS\Bin\makensis.exe')
    }
  }
  $scoopPrefix = (& scoop prefix nsis 2>$null)
  if ($scoopPrefix -and (Test-Path (Join-Path $scoopPrefix 'current\Bin\makensis.exe'))) {
    return (Join-Path $scoopPrefix 'current\Bin\makensis.exe')
  }
  return $null
}
$makensis = Find-Makensis
if (-not $makensis) {
  if (Get-Command scoop -ErrorAction SilentlyContinue) {
    Write-Host '  [install] scoop install nsis ...'
    scoop install nsis
    $makensis = Find-Makensis
  } elseif (Test-Path (Join-Path $repo 'install_nsis.bat')) {
    Write-Host '  [install] 运行 install_nsis.bat (下载 NSIS 安装包并静默安装) ...'
    cmd /c (Join-Path $repo 'install_nsis.bat')
    $makensis = Find-Makensis
  }
  if (-not $makensis) { Die '无法定位或安装 NSIS' }
}
Write-Host "  makensis = $makensis"

# ---------------------------------------------------------------------------
Step "5/5 生成安装包"
Push-Location (Join-Path $repo 'output')
try {
  & $makensis "/DWEASEL_VERSION=$weaselVersion" "/DWEASEL_BUILD=$weaselBuild" "/DPRODUCT_VERSION=$productVersion" install.nsi
  $ok = ($LASTEXITCODE -eq 0)
} finally {
  Pop-Location
}
if (-not $ok) { Die 'makensis 失败' }

$installer = Join-Path $repo "output\archives\weasel-$productVersion-installer.exe"
if (Test-Path $installer) {
  Write-Host "`n=== 安装包已生成 ===" -ForegroundColor Green
  Write-Host "  $installer"
} else {
  Write-Host "`n=== makensis 完成，但未找到预期的安装包: $installer ===" -ForegroundColor Yellow
}
