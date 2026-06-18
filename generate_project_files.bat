@echo off
setlocal enableextensions
pushd "%~dp0"

set "premake=tools\premake5.exe"
set "lua=tools/premake.lua"
set "interactive=1"

if not "%~1"=="" (
    set "interactive=0"
    call :run_choice %~1
    goto :end
)

:menu
cls
echo =============================================
echo          spartan engine project generator
echo =============================================
echo.
echo   [1] visual studio 2026 - vulkan
echo   [2] visual studio 2026 - d3d12 (wip)
echo   [3] gmake2 - vulkan (linux)
echo   [0] exit
echo.
set /p choice="enter your choice: "
call :run_choice %choice%

:end
if "%interactive%"=="1" (
    echo.
    pause
)
popd
endlocal
exit /b %errorlevel%

:run_choice
if "%~1"=="1" (
    "%premake%" --file=%lua% vs2026 vulkan
    goto :eof
)
if "%~1"=="2" (
    "%premake%" --file=%lua% vs2026 d3d12
    goto :eof
)
if "%~1"=="3" (
    "%premake%" --file=%lua% gmake2 vulkan
    goto :eof
)
if "%~1"=="0" goto :eof
echo invalid choice: %~1
exit /b 1
