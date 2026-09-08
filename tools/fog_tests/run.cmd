@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FOG_VS_ROOT=%%i"
if not defined FOG_VS_ROOT exit /b 1
call "%FOG_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
if not exist binaries\fog_tests mkdir binaries\fog_tests
cl /nologo /std:c++20 /EHsc /MT /O2 tools\fog_tests\transport.cpp /Fobinaries\fog_tests\transport.obj /Febinaries\fog_tests\transport.exe /link d3d11.lib d3dcompiler.lib dxgi.lib
if errorlevel 1 exit /b 1
binaries\fog_tests\transport.exe
if errorlevel 1 exit /b 1
node tools\fog_tests\compile_shaders.mjs
