rem Customize your build environment and save the modified copy to env.bat
set "DEVTOOLS_PATH=C:\Program Files\Git\bin;C:\Program Files\Git\cmd;"
set WEASEL_ROOT=%CD%

rem REQUIRED: path to Boost source directory
if not defined BOOST_ROOT set BOOST_ROOT=%WEASEL_ROOT%\deps\boost_1_92_0

rem OPTIONAL: architecture, Visual Studio version and platform toolset
rem set ARCH=Win32
set BJAM_TOOLSET=msvc-14.3
set CMAKE_GENERATOR="Visual Studio 17 2022"
set PLATFORM_TOOLSET=v143
set XMAKE_VS=2022
set XMAKE_VS_TOOLSET=14.3

rem OPTIONAL: path to additional build tools
rem set DEVTOOLS_PATH=%ProgramFiles%\Git\cmd;%ProgramFiles%\Git\usr\bin;%ProgramFiles%\CMake\bin;
