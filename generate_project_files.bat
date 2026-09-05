@echo off
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
