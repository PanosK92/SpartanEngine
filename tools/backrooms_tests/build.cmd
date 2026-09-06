@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BACKROOMS_VS_ROOT=%%i"
if not defined BACKROOMS_VS_ROOT exit /b 1
call "%BACKROOMS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0\..\.."
if not exist binaries\backrooms_tests mkdir binaries\backrooms_tests
rem The bundled Lua library includes the standard interpreter main function.
link /nologo /machine:x64 /subsystem:console /entry:mainCRTStartup /out:binaries\backrooms_tests\lua.exe third_party\libraries\lua.lib libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib
if errorlevel 1 exit /b 1
binaries\backrooms_tests\lua.exe tools\backrooms_tests\generator.lua
