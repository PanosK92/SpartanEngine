@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BLOOM_VS_ROOT=%%i"
if not defined BLOOM_VS_ROOT exit /b 1
call "%BLOOM_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
node tools\bloom_tests\generate.mjs
if errorlevel 1 exit /b 1
node tools\bloom_tests\generate_pass.mjs
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /Ibinaries\bloom_tests tools\bloom_tests\pass.cpp /Fobinaries\bloom_tests\pass.obj /Febinaries\bloom_tests\pass.exe
if errorlevel 1 exit /b 1
binaries\bloom_tests\pass.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 tools\bloom_tests\gpu.cpp /Fobinaries\bloom_tests\gpu.obj /Febinaries\bloom_tests\gpu.exe /link d3d11.lib d3dcompiler.lib dxgi.lib
if errorlevel 1 exit /b 1
binaries\bloom_tests\gpu.exe
