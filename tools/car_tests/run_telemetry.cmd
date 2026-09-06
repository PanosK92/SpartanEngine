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
set "telemetry_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "telemetry_vs=%%i"
if not defined telemetry_vs exit /b 1
call "%telemetry_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "binaries\car_tests" mkdir "binaries\car_tests"
rem Reuse the engine's compiled ImGui objects, including its font configuration.
if not defined SPARTAN_IMGUI_OBJECT_DIR set "SPARTAN_IMGUI_OBJECT_DIR=binaries/obj/x64/development"
cl /nologo /std:c++20 /EHsc /O2 /MT /W3 /WX /permissive- /utf-8 /I source /I source/editor /external:I third_party/free_image /external:W0 /Fo"binaries/car_tests/telemetry_preview.obj" /Fe"binaries/car_tests/telemetry_preview.exe" tools/car_tests/telemetry_preview.cpp "%SPARTAN_IMGUI_OBJECT_DIR%/pch.obj" "%SPARTAN_IMGUI_OBJECT_DIR%/imgui.obj" "%SPARTAN_IMGUI_OBJECT_DIR%/imgui_draw.obj" "%SPARTAN_IMGUI_OBJECT_DIR%/imgui_tables.obj" "%SPARTAN_IMGUI_OBJECT_DIR%/imgui_widgets.obj" "%SPARTAN_IMGUI_OBJECT_DIR%/imgui_freetype.obj" /link /LIBPATH:third_party/libraries freetype.lib FreeImageLib.lib user32.lib gdi32.lib shell32.lib ole32.lib imm32.lib
if errorlevel 1 exit /b 1
binaries\car_tests\telemetry_preview.exe %*


