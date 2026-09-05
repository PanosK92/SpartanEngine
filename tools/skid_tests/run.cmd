@echo off
setlocal
cd /d "%~dp0\..\.."
set "skid_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "skid_test_vs=%%i"
if not defined skid_test_vs exit /b 1
call "%skid_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\skid_tests" mkdir "binaries\skid_tests"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I source /I source/core /Fo"binaries/skid_tests/" /Fe"binaries/skid_tests/tests.exe" tools/skid_tests/main.cpp
if errorlevel 1 exit /b 1
binaries\skid_tests\tests.exe
