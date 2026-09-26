@echo off
setlocal
cd /d "%~dp0"
set "logArg="
if /I "%~1"=="-Log" set "logArg=-Log"
if not "%~1"=="" if not defined logArg goto usage
if not "%~2"=="" goto usage
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-GrimDawn-With-Overlay.ps1" %logArg%
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
:usage
echo Usage: Launch-GrimDawn-With-Overlay.cmd [-Log]
exit /b 1
