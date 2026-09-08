@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RESOLUTION_VS_ROOT=%%i"
if not defined RESOLUTION_VS_ROOT exit /b 1
call "%RESOLUTION_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
node tools\render_options_tests\generate.mjs
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 binaries\render_options_tests\resolution.cpp /Fobinaries\render_options_tests\resolution.obj /Febinaries\render_options_tests\resolution.exe
if errorlevel 1 exit /b 1
binaries\render_options_tests\resolution.exe
