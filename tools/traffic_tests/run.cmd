@echo off
setlocal
cd /d "%~dp0\..\.."
set "traffic_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "traffic_test_vs=%%i"
if not defined traffic_test_vs exit /b 1
call "%traffic_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist binaries\traffic_tests mkdir binaries\traffic_tests
cl /nologo /std:c++20 /EHsc /O2 /arch:AVX2 /MT /W3 /WX /I source /I source/core tools/traffic_tests/main.cpp /Fo"binaries/traffic_tests/main.obj" /Fe"binaries/traffic_tests/routes.exe"
if errorlevel 1 exit /b 1
binaries\traffic_tests\routes.exe
exit /b %errorlevel%
