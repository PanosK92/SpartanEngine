@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LIGHTING_VS_ROOT=%%i"
if not defined LIGHTING_VS_ROOT exit /b 1
call "%LIGHTING_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
node tools\lighting_tests\generate.mjs
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 tools\lighting_tests\cpu.cpp /Fobinaries\lighting_tests\cpu.obj /Febinaries\lighting_tests\cpu.exe
if errorlevel 1 exit /b 1
binaries\lighting_tests\cpu.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 tools\lighting_tests\gpu.cpp /Fobinaries\lighting_tests\gpu.obj /Febinaries\lighting_tests\gpu.exe /link d3d11.lib d3dcompiler.lib dxgi.lib
if errorlevel 1 exit /b 1
binaries\lighting_tests\gpu.exe
