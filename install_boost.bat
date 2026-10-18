@echo off
setlocal

if not defined RIME_ROOT set "RIME_ROOT=%CD%"

if not defined boost_version set "boost_version=1.84.0"
set "boost_x_y_z=%boost_version:.=_%"

if not defined BOOST_ROOT set "BOOST_ROOT=%RIME_ROOT%\deps\boost_%boost_x_y_z%"

call :boost_source_complete
if not errorlevel 1 goto boost_found

for %%I in ("%BOOST_ROOT%\..") do set "src_dir=%%~fI"
if not exist "%src_dir%" mkdir "%src_dir%"
if errorlevel 1 exit /b 1

rem Download the full Boost source archive. Extraction is allowed to repair a
rem partially populated standard boost_x_y_z directory, but the exact
rem BOOST_ROOT requested by the caller is revalidated afterwards.
aria2c "https://archives.boost.io/release/%boost_version%/source/boost_%boost_x_y_z%.7z" -d "%src_dir%"
if errorlevel 1 exit /b 1
pushd "%src_dir%"
7z x "boost_%boost_x_y_z%.7z" -y
if errorlevel 1 (
  popd
  exit /b 1
)
popd

call :boost_source_complete
if errorlevel 1 (
  echo Error: Boost source tree at "%BOOST_ROOT%" is incomplete after extraction.
  echo Expected boost\version.hpp, bootstrap.bat, boost-build.jam and tools\build\src\build-system.jam.
  exit /b 1
)

:boost_found
call .\build.bat boost
exit /b %errorlevel%

:boost_source_complete
if not exist "%BOOST_ROOT%\boost\version.hpp" exit /b 1
if not exist "%BOOST_ROOT%\bootstrap.bat" exit /b 1
if not exist "%BOOST_ROOT%\boost-build.jam" exit /b 1
if not exist "%BOOST_ROOT%\tools\build\src\build-system.jam" exit /b 1
exit /b 0
