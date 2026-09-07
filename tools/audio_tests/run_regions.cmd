@echo off
setlocal
cd /d "%~dp0\..\.."
set "region_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "region_test_vs=%%i"
if not defined region_test_vs exit /b 1
call "%region_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist binaries\audio_tests mkdir binaries\audio_tests
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /WX tools/audio_tests/regions.cpp /Fo"binaries/audio_tests/regions.obj" /Fe"binaries/audio_tests/regions.exe"
if errorlevel 1 exit /b 1
binaries\audio_tests\regions.exe
