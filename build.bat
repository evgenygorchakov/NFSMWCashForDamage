@echo off
rem Builds dist\NFSMWCashForDamage.asi (Win32, static CRT) and runs the unit tests.
rem Keep this file ASCII: cmd reads .bat files in the OEM code page.
rem No parenthesized blocks: the ")" in "(x86)" below would close them.
setlocal

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" goto :novc
rem vcvarsall looks for vswhere.exe in PATH.
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "%VCVARS%" x86 >nul
if errorlevel 1 goto :fail

cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail
cmake --build build
if errorlevel 1 goto :fail
ctest --test-dir build --output-on-failure
if errorlevel 1 goto :fail

if not exist dist mkdir dist
copy /y build\NFSMWCashForDamage.asi dist\ >nul
if errorlevel 1 goto :fail
copy /y NFSMWCashForDamage.ini dist\ >nul
if errorlevel 1 goto :fail

echo.
echo Done: dist\NFSMWCashForDamage.asi and dist\NFSMWCashForDamage.ini
exit /b 0

:novc
echo vcvarsall.bat not found: "%VCVARS%"
exit /b 1

:fail
echo.
echo BUILD FAILED
exit /b 1
