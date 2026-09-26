@echo off
rem Find an installed Visual Studio C++ toolchain and select its x64 compiler.
set "overlayVswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%overlayVswhere%" (
    echo Visual Studio Installer's vswhere.exe was not found. 1>&2
    exit /b 1
)
set "overlayVSInstall="
for /f "usebackq delims=" %%I in (`"%overlayVswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "overlayVSInstall=%%I"
if not defined overlayVSInstall (
    echo No Visual Studio installation with the MSVC x64 toolchain was found. 1>&2
    exit /b 1
)
if not exist "%overlayVSInstall%\VC\Auxiliary\Build\vcvars64.bat" (
    echo The selected Visual Studio installation has no vcvars64.bat. 1>&2
    exit /b 1
)
call "%overlayVSInstall%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
where cl.exe >nul 2>nul
if errorlevel 1 (
    echo MSVC compiler was not found after vcvars64.bat. 1>&2
    exit /b 1
)
exit /b 0
