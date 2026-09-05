@echo off
setlocal
cd /d "%~dp0\..\.."
set "audio_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "audio_test_vs=%%i"
if not defined audio_test_vs exit /b 1
call "%audio_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\audio_tests" mkdir "binaries\audio_tests"
cl /nologo /std:c++20 /EHsc /O2 /MT /W3 /WX /permissive- /utf-8 /I tools/audio_tests /I source/car /Fo"binaries/audio_tests/" /Fe"binaries/audio_tests/render.exe" tools/audio_tests/render.cpp source/car/CarEngineSoundSynthesis.cpp /link /LIBPATH:third_party/libraries assimp.lib
if errorlevel 1 exit /b 1
binaries\audio_tests\render.exe %*
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /Gy /W3 /WX /permissive- /utf-8 /Zc:preprocessor /FInew /I source /I source/core /I third_party/physx /I third_party/physx/physx /Fo"binaries/audio_tests/" /Fe"binaries/audio_tests/preset_loading.exe" tools/audio_tests/preset_loading.cpp source/car/CarPresets.cpp /link /OPT:REF /LIBPATH:third_party/libraries assimp.lib
if errorlevel 1 exit /b 1
binaries\audio_tests\preset_loading.exe
