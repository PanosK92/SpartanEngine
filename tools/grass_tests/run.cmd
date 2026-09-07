@echo off
setlocal
cd /d "%~dp0\..\.."
node tools/grass_tests/compile_shaders.mjs
if errorlevel 1 exit /b 1
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GRASS_VS_ROOT=%%i"
if not defined GRASS_VS_ROOT exit /b 1
call "%GRASS_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tools\grass_tests\scattering.cpp /Fobinaries\grass_tests\scattering.obj /Febinaries\grass_tests\scattering.exe
if errorlevel 1 exit /b 1
binaries\grass_tests\scattering.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I"%VULKAN_SDK%\Include" tools\grass_tests\gpu.cpp /Fobinaries\grass_tests\gpu.obj /Febinaries\grass_tests\gpu.exe /link /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib
if errorlevel 1 exit /b 1
binaries\grass_tests\gpu.exe
