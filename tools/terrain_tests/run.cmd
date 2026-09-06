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
set "terrain_test_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "terrain_test_vs=%%i"
if not defined terrain_test_vs exit /b 1
call "%terrain_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\terrain_tests" mkdir "binaries\terrain_tests"
cl /nologo /std:c++20 /EHsc /O2 /arch:AVX2 /MT /W3 /WX /Gy /I source /I source/core /Fo"binaries/terrain_tests/" /Fe"binaries/terrain_tests/terrain_tests.exe" tools/terrain_tests/terrain_tests.cpp tools/terrain_tests/adapters.cpp source/world/TerrainSystem.cpp source/rendering/Instance.cpp source/math/Vector2.cpp source/math/Vector3.cpp source/math/Vector4.cpp source/math/Quaternion.cpp source/math/Matrix.cpp source/math/BoundingBox.cpp source/rendering/Color.cpp /link /OPT:REF
if errorlevel 1 exit /b 1
binaries\terrain_tests\terrain_tests.exe
