@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VHS_VS_ROOT=%%i"
if not defined VHS_VS_ROOT exit /b 1
call "%VHS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
if not exist binaries\vhs_tests mkdir binaries\vhs_tests
cl /nologo /std:c++20 /EHsc /MT /O2 tools\vhs_tests\playback.cpp /Fobinaries\vhs_tests\playback.obj /Febinaries\vhs_tests\playback.exe /link d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
binaries\vhs_tests\playback.exe
