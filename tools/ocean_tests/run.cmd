@echo off
rem Copyright(c) 2015-2026 Panos Karabelas
rem
rem Permission is hereby granted, free of charge, to any person obtaining a copy
rem of this software and associated documentation files (the "Software"), to deal
rem in the Software without restriction, including without limitation the rights
rem to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
rem copies of the Software, and to permit persons to whom the Software is furnished
rem to do so, subject to the following conditions :
rem
rem The above copyright notice and this permission notice shall be included in
rem all copies or substantial portions of the Software.
rem
rem THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
rem IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
rem FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
rem COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
rem IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
rem CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

setlocal
cd /d "%~dp0\..\.."
set "ocean_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ocean_test_vs=%%i"
if not defined ocean_test_vs exit /b 1
call "%ocean_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
msbuild spartan.vcxproj /p:Configuration=development /p:Platform=x64 /p:IntDir=binaries/ocean_tests/obj/ /p:TargetName=spartan_ocean_validation /m:4 /nologo /v:minimal
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /W3 /WX /Zc:preprocessor /DDEVELOPMENT /I source /I source/core /I third_party/lua /c tools/ocean_tests/component_tests.cpp /Fo"binaries/ocean_tests/component_tests.obj"
if errorlevel 1 exit /b 1
node tools/ocean_tests/prepare_link.mjs
if errorlevel 1 exit /b 1
link @binaries/ocean_tests/component_link.rsp
if errorlevel 1 exit /b 1
binaries\spartan_ocean_component_tests.exe
if errorlevel 1 exit /b 1
node tools/ocean_tests/compile_shaders.mjs
exit /b %errorlevel%
