@echo off
rem Copyright(c) 2015-2026 Panos Karabelas
rem Licensed under the Spartan Engine License. See license.md in the repository root.
rem https://github.com/PanosK92/SpartanEngine/blob/master/license.md
rem Commercial use requires written permission and negotiated payment terms.

setlocal enableextensions enabledelayedexpansion
pushd "%~dp0"

set "premake=tools\premake5.exe"
set "lua=tools/premake.lua"

rem non-interactive: generate_project_files.bat <action> [api], e.g. vs2026 d3d12
if not "%~1"=="" (
    "%premake%" --file=%lua% %*
    set "rc=!errorlevel!"
    goto :exit_with_code
)

chcp 65001 >nul 2>&1
title Spartan Engine - Project Generator

for /f "delims=" %%e in ('echo prompt $E^| cmd') do set "ESC=%%e"
set "C_ACCENT=%ESC%[96m"
set "C_CYAN=%ESC%[36m"
set "C_OK=%ESC%[92m"
set "C_WARN=%ESC%[93m"
set "C_ERR=%ESC%[91m"
set "C_TEXT=%ESC%[97m"
set "C_MUTED=%ESC%[90m"
set "C_BOLD=%ESC%[1m"
set "C_RST=%ESC%[0m"

set "fast_mode=0"
set "rc=0"

:menu
cls

rem detect currently generated project backend
set "active_proj=%C_MUTED%none generated yet%C_RST%"
if exist "spartan.vcxproj" (
    findstr /c:"API_GRAPHICS_VULKAN" spartan.vcxproj >nul 2>&1 && set "active_proj=%C_ACCENT%Visual Studio · Vulkan%C_RST%"
    findstr /c:"API_GRAPHICS_D3D12" spartan.vcxproj >nul 2>&1 && set "active_proj=%C_ACCENT%Visual Studio · DirectX 12%C_RST%"
    findstr /c:"API_GRAPHICS_WEBGPU" spartan.vcxproj >nul 2>&1 && set "active_proj=%C_ACCENT%Visual Studio · WebGPU%C_RST%"
)

rem detect cached libraries archive
if exist "third_party\libraries\libraries.7z" (
    set "cache_state=%C_OK%● cached%C_RST%"
) else (
    set "cache_state=%C_WARN%○ will download%C_RST%"
)

if "!fast_mode!"=="1" (
    set "fast_badge=%C_WARN%ON  %C_MUTED%^(skipping setup^)%C_RST%                 "
) else (
    set "fast_badge=%C_MUTED%OFF%C_RST%                                  "
)

echo.
echo   %C_ACCENT%%C_BOLD%███████╗██████╗  █████╗ ██████╗ ████████╗ █████╗ ███╗   ██╗%C_RST%
echo   %C_ACCENT%%C_BOLD%██╔════╝██╔══██╗██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗████╗  ██║%C_RST%
echo   %C_ACCENT%███████╗██████╔╝███████║██████╔╝   ██║   ███████║██╔██╗ ██║%C_RST%
echo   %C_CYAN%╚════██║██╔═══╝ ██╔══██║██╔══██╗   ██║   ██╔══██║██║╚██╗██║%C_RST%
echo   %C_CYAN%███████║██║     ██║  ██║██║  ██║   ██║   ██║  ██║██║ ╚████║%C_RST%
echo   %C_MUTED%╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝%C_RST%
echo   %C_TEXT%%C_BOLD%E N G I N E%C_RST%  %C_MUTED%·  P R O J E C T   G E N E R A T O R%C_RST%
echo.
echo   %C_MUTED%Workspace%C_RST%   !active_proj!   %C_MUTED%│  Libraries%C_RST%  !cache_state!
echo.
echo   %C_CYAN%╭────────────────────────────────────────────────────────────────────────╮%C_RST%
echo   %C_CYAN%│%C_RST%  %C_MUTED%SELECT GRAPHICS BACKEND%C_RST%                                               %C_CYAN%│%C_RST%
echo   %C_CYAN%├────────────────────────────────────────────────────────────────────────┤%C_RST%
echo   %C_CYAN%│%C_RST%                                                                        %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_ACCENT%[1]%C_RST%  %C_BOLD%%C_TEXT%Visual Studio 2026 · DirectX 12%C_RST%   %C_OK%recommended ^(Windows^)%C_RST%         %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%        %C_MUTED%Agility SDK, cross-vendor RT, DLSS 4.5, FSR 3.1, XeSS 3%C_RST%         %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%                                                                        %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_ACCENT%[2]%C_RST%  %C_BOLD%%C_TEXT%Visual Studio 2026 · Vulkan%C_RST%                                     %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%        %C_MUTED%Cross-vendor RT, DLSS 4.5, FSR 3.1, XeSS 3%C_RST%                      %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%                                                                        %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_ACCENT%[3]%C_RST%  %C_BOLD%%C_TEXT%Visual Studio 2026 · WebGPU%C_RST%                                     %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%        %C_MUTED%Google Dawn native  ^(browser build: tools\web_build.py^)%C_RST%          %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%                                                                        %C_CYAN%│%C_RST%
echo   %C_CYAN%├────────────────────────────────────────────────────────────────────────┤%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_TEXT%[S]%C_RST%  %C_TEXT%Sync dependencies only%C_RST%     %C_MUTED%Download, verify ^& stage SDKs%C_RST%        %C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_TEXT%[F]%C_RST%  %C_TEXT%Fast regenerate%C_RST%            !fast_badge!%C_CYAN%│%C_RST%
echo   %C_CYAN%│%C_RST%   %C_BOLD%%C_MUTED%[0]%C_RST%  %C_MUTED%Exit%C_RST%                                                            %C_CYAN%│%C_RST%
echo   %C_CYAN%╰────────────────────────────────────────────────────────────────────────╯%C_RST%
echo.
<nul set /p="  %C_ACCENT%▸%C_RST% %C_TEXT%Press a key %C_MUTED%[1 / 2 / 3 / S / F / 0]%C_RST%: "

choice /c 123sf0 /n >nul
set "pick=%errorlevel%"

if "%pick%"=="6" goto :exit_ok
if "%pick%"=="5" (
    if "!fast_mode!"=="1" (
        set "fast_mode=0"
    ) else (
        set "fast_mode=1"
    )
    goto :menu
)

set "args="
set "is_setup_only=0"
if "%pick%"=="1" set "args=vs2026 d3d12"
if "%pick%"=="2" set "args=vs2026 vulkan"
if "%pick%"=="3" set "args=vs2026 webgpu"
if "%pick%"=="4" (
    set "args=setup"
    set "is_setup_only=1"
)

if "!is_setup_only!"=="0" if "!fast_mode!"=="1" set "args=!args! --skip-setup"

cls
"%premake%" --file=%lua% !args!
set "rc=!errorlevel!"

if "!rc!"=="0" goto :run_ok
goto :run_fail

:run_ok
if "!is_setup_only!"=="1" (
    echo   %C_ACCENT%▸%C_RST% %C_TEXT%Press %C_BOLD%[R]%C_RST%%C_TEXT% for menu or %C_BOLD%[Q]%C_RST%%C_TEXT% to exit%C_RST%
    choice /c rq /n >nul
    if "!errorlevel!"=="1" goto :menu
    goto :exit_ok
)

set "sln_file=spartan.slnx"
if not exist "!sln_file!" if exist "spartan.sln" set "sln_file=spartan.sln"

echo.
echo   %C_OK%╭────────────────────────────────────────────────────────────────────────╮%C_RST%
echo   %C_OK%│%C_RST%  %C_BOLD%%C_OK%✓  Solution ready%C_RST%   %C_MUTED%!sln_file! generated in repository root%C_RST%         %C_OK%│%C_RST%
echo   %C_OK%╰────────────────────────────────────────────────────────────────────────╯%C_RST%
echo.
echo   %C_ACCENT%▸%C_RST% %C_TEXT%Press %C_BOLD%%C_OK%[O]%C_RST%%C_TEXT% to open !sln_file!, %C_BOLD%[R]%C_RST%%C_TEXT% for menu, or %C_BOLD%[Q]%C_RST%%C_TEXT% to exit%C_RST%
choice /c orq /n >nul
set "post=%errorlevel%"
if "%post%"=="1" (
    if exist "!sln_file!" start "" "!sln_file!"
    goto :exit_ok
)
if "%post%"=="2" goto :menu
goto :exit_ok

:run_fail
echo.
echo   %C_ERR%╭────────────────────────────────────────────────────────────────────────╮%C_RST%
echo   %C_ERR%│%C_RST%  %C_BOLD%%C_ERR%✗  Project generation failed%C_RST%  %C_MUTED%check the output above for details%C_RST%        %C_ERR%│%C_RST%
echo   %C_ERR%╰────────────────────────────────────────────────────────────────────────╯%C_RST%
echo   %C_MUTED%   Exit code: !rc!%C_RST%
echo.
echo   %C_ACCENT%▸%C_RST% %C_TEXT%Press %C_BOLD%[R]%C_RST%%C_TEXT% to return to menu or %C_BOLD%[Q]%C_RST%%C_TEXT% to exit%C_RST%
choice /c rq /n >nul
if "!errorlevel!"=="1" goto :menu
goto :exit_with_code

:exit_ok
set "rc=0"

:exit_with_code
popd
endlocal & exit /b %rc%
