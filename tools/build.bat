@echo off
setlocal
rem ---------------------------------------------------------------------------
rem build.bat [clean] [target...]
rem
rem On Chinese MSVC installations, CMake/Ninja can persist a mis-encoded
rem localized /showIncludes prefix. Header dependencies are then silently
rem lost, so stale .obj files (and even mismatched object layouts) survive.
rem This wrapper cleans first whenever a project header is newer than the last
rem successful build stamp. Keep the batch file ASCII-only for cmd.exe.
rem ---------------------------------------------------------------------------
set VSLANG=1033

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "VCVARS=E:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=E:\Qt\Tools\CMake_64\bin\cmake.exe"
set "BUILD=%ROOT%\build"
set "STAMP=%BUILD%\.header-stamp"

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo [build] vcvars64 failed
  exit /b 1
)

set "MODE=clean"
for /f "delims=" %%M in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\check_header_changes.ps1" -Root "%ROOT%" -Stamp "%STAMP%"') do set "MODE=%%M"

if /i "%~1"=="clean" goto explicit_clean
goto mode_ready

:explicit_clean
set "MODE=clean"
shift

:mode_ready
if /i "%MODE%"=="clean" (
  echo [build] header changed or explicit clean; cleaning old objects...
  "%CMAKE%" --build "%BUILD%" --target clean >nul
  if errorlevel 1 exit /b 1
)

if "%~1"=="" (
  "%CMAKE%" --build "%BUILD%"
) else (
  "%CMAKE%" --build "%BUILD%" --target %*
)
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -Command "New-Item -ItemType File -Force -Path '%STAMP%' | Out-Null"
if errorlevel 1 exit /b 1
exit /b 0
