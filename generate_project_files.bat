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

setlocal enableextensions
pushd "%~dp0"

set "premake=tools\premake5.exe"
set "lua=tools/premake.lua"

rem non interactive: generate_project_files.bat <action> <api>, e.g. vs2026 vulkan
if not "%~1"=="" (
    "%premake%" --file=%lua% %*
    goto :end
)

:menu
cls
echo =============================================
echo          spartan engine project generator
echo =============================================
echo.
echo   [1] visual studio 2026 - vulkan
echo   [2] visual studio 2026 - d3d12
echo   [3] gmake2 - vulkan (linux)
echo   [0] exit
echo.
set /p choice="enter your choice: "

if "%choice%"=="0" goto :end
if "%choice%"=="1" set "args=vs2026 vulkan"
if "%choice%"=="2" set "args=vs2026 d3d12"
if "%choice%"=="3" set "args=gmake2 vulkan"
if not defined args (
    echo invalid choice: %choice%
    goto :end
)
"%premake%" --file=%lua% %args%
echo.
pause

:end
popd
endlocal
exit /b %errorlevel%
