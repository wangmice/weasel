@echo off
set "WEASEL_VERSION_FILE=%~dp0..\VERSION"
if not exist "%WEASEL_VERSION_FILE%" (
  echo Error: VERSION file not found: "%WEASEL_VERSION_FILE%".
  exit /b 1
)

set "WEASEL_VERSION="
set "VERSION_MAJOR="
set "VERSION_MINOR="
set "VERSION_PATCH="
set "VERSION_EXTRA="
set /p WEASEL_VERSION=<"%WEASEL_VERSION_FILE%"

for /f "tokens=1-4 delims=." %%a in ("%WEASEL_VERSION%") do (
  set "VERSION_MAJOR=%%a"
  set "VERSION_MINOR=%%b"
  set "VERSION_PATCH=%%c"
  set "VERSION_EXTRA=%%d"
)

if not defined VERSION_MAJOR goto invalid_version
if not defined VERSION_MINOR goto invalid_version
if not defined VERSION_PATCH goto invalid_version
if defined VERSION_EXTRA goto invalid_version
if not "%WEASEL_VERSION%"=="%VERSION_MAJOR%.%VERSION_MINOR%.%VERSION_PATCH%" goto invalid_version
for /f "delims=0123456789" %%a in ("%VERSION_MAJOR%%VERSION_MINOR%%VERSION_PATCH%") do goto invalid_version
exit /b 0

:invalid_version
echo Error: VERSION must contain exactly MAJOR.MINOR.PATCH using decimal digits.
exit /b 1
