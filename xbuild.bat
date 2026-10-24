@echo off
setlocal enabledelayedexpansion
rem ---------------------------------------------------------------------------
if not exist env.bat copy env.bat.template env.bat
if exist env.bat call env.bat
set PRODUCT_VERSION=
if not defined WEASEL_ROOT set WEASEL_ROOT=%CD%
call "%~dp0update\load-version.bat"
if errorlevel 1 exit /b 1
if not defined XMAKE_VS set XMAKE_VS=2022
if not defined XMAKE_VS_TOOLSET set XMAKE_VS_TOOLSET=14.3
set "xmake_msvc_options=--vs=%XMAKE_VS% --vs_toolset=%XMAKE_VS_TOOLSET%"

if not defined WEASEL_BUILD set WEASEL_BUILD=0

rem use numeric build version for release build
set PRODUCT_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%
rem for non-release build, try to use git commit hash as product build version
if not defined RELEASE_BUILD (
  rem check if git is installed and available, then get the short commit id of head
  git --version >nul 2>&1
  if not errorlevel 1 (
    for /f "delims=" %%i in ('git tag --sort=-creatordate ^| findstr /r "%WEASEL_VERSION%"') do (
      set LAST_TAG=%%i
      goto found_tag
    )
    :found_tag
    for /f "delims=" %%i in ('git rev-list %LAST_TAG%..HEAD --count') do (
      set WEASEL_BUILD=%%i
    )
    rem get short commmit id of head
    for /F %%i in ('git rev-parse --short HEAD') do (set PRODUCT_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%.%%i)
  )
)

rem FILE_VERSION is always 4 numbers; same as PRODUCT_VERSION in release build
if not defined FILE_VERSION set FILE_VERSION=%WEASEL_VERSION%.%WEASEL_BUILD%
echo PRODUCT_VERSION=%PRODUCT_VERSION%
echo WEASEL_VERSION=%WEASEL_VERSION%
echo WEASEL_BUILD=%WEASEL_BUILD%
echo WEASEL_ROOT=%WEASEL_ROOT%
echo WEASEL_BUNDLED_RECIPES=%WEASEL_BUNDLED_RECIPES%
echo BOOST_ROOT=%BOOST_ROOT%
echo XMAKE_VS=%XMAKE_VS%
echo XMAKE_VS_TOOLSET=%XMAKE_VS_TOOLSET%

if defined GITHUB_ENV (
	setlocal enabledelayedexpansion
	echo git_ref_name=%PRODUCT_VERSION%>>!GITHUB_ENV!
)

rem ---------------------------------------------------------------------------
rem parse the command line options
set build_config=release
set build_rebuild=0
set build_boost=0
set boost_build_variant=release
set build_data=0
set build_opencc=0
set build_rime=0
set rime_build_variant=release
set build_weasel=0
set build_installer=0
set build_arm64=0
set build_clean=0
set build_commands=0
:parse_cmdline_options
  if "%1" == "" goto end_parsing_cmdline_options
  if "%1" == "debug" (
    set build_config=debug
    set boost_build_variant=debug
    set rime_build_variant=debug
  )
  if "%1" == "release" (
    set build_config=release
    set boost_build_variant=release
    set rime_build_variant=release
  )
  if "%1" == "rebuild" set build_rebuild=1
  if "%1" == "boost" set build_boost=1
  if "%1" == "data" set build_data=1
  if "%1" == "opencc" set build_opencc=1
  if "%1" == "rime" set build_rime=1
  if "%1" == "librime" set build_rime=1
  if "%1" == "weasel" set build_weasel=1
  if "%1" == "installer" set build_installer=1
  if "%1" == "arm64" set build_arm64=1
  if "%1" == "clean" set build_clean=1
  if "%1" == "commands" set build_commands=1
  if "%1" == "all" (
    set build_boost=1
    set build_data=1
    set build_opencc=1
    set build_rime=1
    set build_weasel=1
    set build_installer=1
    set build_arm64=1
    set build_commands=1
  )
  shift
  goto parse_cmdline_options
:end_parsing_cmdline_options

if %build_weasel% == 0 (
if %build_boost% == 0 (
if %build_data% == 0 (
if %build_opencc% == 0 (
if %build_rime% == 0 (
if %build_commands% == 0 (
  set build_weasel=1
))))))
rem 
rem quit WeaselServer.exe before building
cd /d %WEASEL_ROOT%
if exist output\weaselserver.exe (
  output\weaselserver.exe /q
)

rem build booost
if %build_boost% == 1 (
  if %build_arm64% == 1 (
    call build.bat boost arm64
  ) else (
    call build.bat boost
  )
  if errorlevel 1 exit /b 1
  cd /d %WEASEL_ROOT%
)
if %build_rime% == 1 (
  call build.bat rime
  if errorlevel 1 exit /b 1
  cd /d %WEASEL_ROOT%
)
if %build_data% == 1 (
  call build.bat data
  if errorlevel 1 exit /b 1
  cd /d %WEASEL_ROOT%
)
if %build_opencc% == 1 (
  call build.bat opencc
  if errorlevel 1 exit /b 1
  cd /d %WEASEL_ROOT%
)

if %build_commands% == 1 (
  echo Generating compile_commands.json
  xmake project -k compile_commands -m %build_config%
  if errorlevel 1 goto error
)

if defined SDKVER set build_sdk_option=--vs_sdkver=%SDKVER% -c
if not defined SDKVER set build_sdk_option=

rem if to clean
if %build_clean% == 1 ( goto clean )
if %build_weasel% == 0 ( goto end )

rem Resolve the VS2022 instance once, but never trust the caller's active target
rem architecture. xmake can otherwise inherit an x64 Developer Prompt and use
rem HostX64\x64\cl.exe even after `xmake f -a x86`.
set "XMAKE_VS_INSTALL="
set "XMAKE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%XMAKE_VSWHERE%" (
  echo Error: vswhere.exe not found at "%XMAKE_VSWHERE%".
  goto error
)
rem Do not execute a quoted Program Files path through FOR /F command
rem substitution: the nested cmd.exe can reparse it as C:\Program. Capture the
rem normal quoted command's output first, then read the first line separately.
set "XMAKE_VSWHERE_OUTPUT=%TEMP%\weasel-vswhere-%RANDOM%-%RANDOM%.txt"
"%XMAKE_VSWHERE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%XMAKE_VSWHERE_OUTPUT%"
if errorlevel 1 (
  del /q "%XMAKE_VSWHERE_OUTPUT%" >nul 2>&1
  echo Error: vswhere failed to locate Visual Studio 2022.
  goto error
)
set /p XMAKE_VS_INSTALL=<"%XMAKE_VSWHERE_OUTPUT%"
del /q "%XMAKE_VSWHERE_OUTPUT%" >nul 2>&1
if not defined XMAKE_VS_INSTALL (
  echo Error: Visual Studio 2022 C++ toolchain not found.
  goto error
)
set "XMAKE_VCVARSALL=%XMAKE_VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%XMAKE_VCVARSALL%" (
  echo Error: vcvarsall.bat not found at "%XMAKE_VCVARSALL%".
  goto error
)
echo XMAKE_VS_INSTALL=%XMAKE_VS_INSTALL%

if %build_arm64% == 1 (
  call :build_xmake_arch arm64 x64_arm64 arm64
  if errorlevel 1 goto error
  call :build_xmake_arch arm x64_arm arm
  if errorlevel 1 goto error
)
call :build_xmake_arch x64 x64 x64
if errorlevel 1 goto error
call :build_xmake_arch x86 x64_x86 x86
if errorlevel 1 goto error

if %build_arm64% == 1 (
  pushd arm64x_wrapper
  call build.bat
  if errorlevel 1 goto error
  popd

  copy arm64x_wrapper\weaselARM64X.dll output
  if errorlevel 1 goto error
)
if %build_installer% == 1 (
  "%ProgramFiles(x86)%"\NSIS\Bin\makensis.exe ^
  /DWEASEL_VERSION=%WEASEL_VERSION% ^
  /DWEASEL_BUILD=%WEASEL_BUILD% ^
  /DPRODUCT_VERSION=%PRODUCT_VERSION% ^
  output\install.nsi
  if errorlevel 1 goto error
)
goto end

rem ---------------------------------------------------------------------------
rem Build one target architecture in a fresh, x64-hosted Visual Studio target
rem environment. setlocal prevents vcvarsall changes from leaking into the next
rem architecture. Pass the validated compiler/linker paths as xmake configure
rem values; the Windows/MSVC toolchain consumes config.get("cc"/"cxx"/etc.),
rem so environment-only CC/CXX/LD/SH/AR overrides are insufficient here.
:build_xmake_arch
  setlocal
  set "xmake_arch=%~1"
  set "vcvars_arch=%~2"
  set "expected_target=%~3"

  rem The workflow enters this script from an x64 Developer Command Prompt.
  rem vcvarsall appends INCLUDE/LIB/LIBPATH, so clear the inherited target
  rem environment before selecting the next architecture. Otherwise ARM builds
  rem retain x64 import-library paths and may mix SDK versions.
  set "VSINSTALLDIR="
  set "INCLUDE="
  set "LIB="
  set "LIBPATH="

  if defined SDKVER (
    call "%XMAKE_VCVARSALL%" %vcvars_arch% %SDKVER%
  ) else (
    call "%XMAKE_VCVARSALL%" %vcvars_arch%
  )
  if errorlevel 1 exit /b 1

  set "selected_cl="
  for /f "delims=" %%c in ('where cl 2^>nul') do if not defined selected_cl set "selected_cl=%%c"
  if not defined selected_cl (
    echo Error: cl.exe not found after vcvarsall %vcvars_arch%.
    exit /b 1
  )
  echo xmake %xmake_arch% compiler: !selected_cl!
  echo(!selected_cl!| findstr /i /e /c:"\Hostx64\%expected_target%\cl.exe" >nul
  if errorlevel 1 (
    echo Error: vcvarsall %vcvars_arch% selected the wrong compiler for %xmake_arch%.
    exit /b 1
  )

  for %%p in ("!selected_cl!") do set "selected_bin=%%~dpp"
  set "selected_link=!selected_bin!link.exe"
  if not exist "!selected_link!" (
    echo Error: link.exe not found next to selected compiler: !selected_link!
    exit /b 1
  )
  echo xmake %xmake_arch% linker: !selected_link!
  if /i "%xmake_arch%"=="arm" echo xmake arm LIB: !LIB!

  xmake f -c -a %xmake_arch% -m %build_config% %xmake_msvc_options% %build_sdk_option% "--cc=!selected_cl!" "--cxx=!selected_cl!" "--ld=!selected_link!" "--sh=!selected_link!" "--ar=!selected_link!"
  if errorlevel 1 exit /b 1
  if %build_rebuild% == 1 (
    xmake clean
    if errorlevel 1 exit /b 1
  )
  set "expected_artifact=%WEASEL_ROOT%\build\windows\!xmake_arch!\%build_config%\WeaselTSF\weasel.dll"
  if /i "!xmake_arch!"=="x64" set "expected_artifact=%WEASEL_ROOT%\build\windows\x64\%build_config%\WeaselTSF\weaselx64.dll"
  if /i "!xmake_arch!"=="arm" set "expected_artifact=%WEASEL_ROOT%\build\windows\arm\%build_config%\WeaselTSF\weaselARM.dll"
  if /i "!xmake_arch!"=="arm64" set "expected_artifact=%WEASEL_ROOT%\build\windows\arm64\%build_config%\WeaselTSF\weaselARM64.dll"
  if exist "!expected_artifact!" del /q "!expected_artifact!"

  xmake
  if errorlevel 1 exit /b 1
  if not exist "!expected_artifact!" (
    echo Error: xmake %xmake_arch% did not produce "!expected_artifact!".
    exit /b 1
  )
  endlocal
  exit /b 0

:clean
  if exist build (
    rmdir /s /q build
    if errorlevel 1 (
      echo error cleaning weasel build
      goto error
    )
    goto end
  )

:error
  echo error building weasel...
  exit /b 1
  
:end
  cd %WEASEL_ROOT%
