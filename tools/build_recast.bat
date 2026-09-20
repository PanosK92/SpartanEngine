@echo off
rem Rebuild the x64 static libraries shipped in libraries.7z from the vendored sources.
setlocal
pushd "%~dp0\.."
set "recast_vs="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "recast_vs=%%i"
if not defined recast_vs goto :failed
call "%recast_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :failed
if not exist "third_party\libraries" mkdir "third_party\libraries"
for %%C in (release debug) do (
    for %%L in (Recast Detour DetourCrowd) do (
        call :build %%L %%C
        if errorlevel 1 goto :failed
    )
)
popd
exit /b 0

:build
set "recast_name=%~1"
set "recast_config=%~2"
set "recast_obj=binaries\obj\recast\%recast_config%\%recast_name%"
set "recast_flags=/O2 /MT /DNDEBUG"
set "recast_suffix="
if "%recast_config%"=="debug" (
    set "recast_flags=/Od /MTd /Z7 /D_DEBUG"
    set "recast_suffix=_debug"
)
if not exist "%recast_obj%" mkdir "%recast_obj%"
cl /nologo /c /std:c++20 /EHsc /W3 /WX /permissive- /utf-8 %recast_flags% /I third_party/recast/Recast/Include /I third_party/recast/Detour/Include /I third_party/recast/DetourCrowd/Include /Fo"%recast_obj%\\" third_party/recast/%recast_name%/Source/*.cpp
if errorlevel 1 exit /b 1
lib /nologo /MACHINE:X64 /OUT:"third_party\libraries\%recast_name%%recast_suffix%.lib" "%recast_obj%\*.obj"
exit /b %errorlevel%

:failed
popd
exit /b 1
