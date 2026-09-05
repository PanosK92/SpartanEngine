@echo off
setlocal
cd /d "%~dp0\..\.."
set "car_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "car_test_vs=%%i"
if not defined car_test_vs exit /b 1
call "%car_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\car_tests" mkdir "binaries\car_tests"
cl /nologo /std:c++20 /EHsc /O2 /MT /Gy /W3 /WX /permissive- /utf-8 /Zc:preprocessor /FInew /I source /I source/core /I third_party/physx /I third_party/physx/physx /Fo"binaries/car_tests/" /Fe"binaries/car_tests/headless.exe" tools/car_tests/headless.cpp source/car/CarSimulation.cpp source/car/CarSimulation_Telemetry.cpp source/car/CarPresets.cpp /link /OPT:REF /LIBPATH:third_party/libraries PhysX_static_64.lib PhysXCommon_static_64.lib PhysXFoundation_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXCooking_static_64.lib PhysXCharacterKinematic_static_64.lib assimp.lib
if errorlevel 1 exit /b 1
binaries\car_tests\headless.exe %*

