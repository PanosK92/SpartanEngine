@echo off
setlocal
cd /d "%~dp0\..\.."
set "collision_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "collision_test_vs=%%i"
if not defined collision_test_vs exit /b 1
call "%collision_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\car_tests" mkdir "binaries\car_tests"
cl /nologo /std:c++20 /EHsc /O2 /MT /W3 /WX /DNDEBUG /permissive- /utf-8 /I source /I third_party/physx /I third_party/physx/physx /I third_party/assimp /Fo"binaries/car_tests/collision.obj" /Fe"binaries/car_tests/collision.exe" tools/car_tests/collision.cpp /link /LIBPATH:third_party/libraries PhysX_static_64.lib PhysXCommon_static_64.lib PhysXFoundation_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXCooking_static_64.lib assimp.lib FreeImageLib.lib
if errorlevel 1 exit /b 1
binaries\car_tests\collision.exe %*


