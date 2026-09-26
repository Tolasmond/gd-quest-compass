@echo off
setlocal
cd /d "%~dp0"
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
set "buildDir=build"
if "%~1"=="staged" set "buildDir=build-staged"
if not exist "%buildDir%" mkdir "%buildDir%"
pushd "%buildDir%"
cl /nologo /std:c++17 /EHsc /W4 /MD /O2 /utf-8 /LD /I"..\vendor\Detours\src" ..\src\position_overlay.cpp ..\vendor\Detours\src\detours.cpp ..\vendor\Detours\src\modules.cpp ..\vendor\Detours\src\disasm.cpp ..\vendor\Detours\src\image.cpp ..\vendor\Detours\src\creatwth.cpp /link /OUT:position-overlay.dll user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /MD /O2 /utf-8 ..\src\loader.cpp /Fe:position-loader.exe
if errorlevel 1 exit /b 1
popd
echo Build complete.
