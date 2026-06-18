@echo off
setlocal enableextensions
pushd "%~dp0\.."

set "libraries=third_party\libraries\libraries.7z"
set "project=binaries\project\project.7z"

echo local file hashes:
echo.

call :print_hash libraries "%libraries%"
call :print_hash project   "%project%"

echo.
pause
popd
endlocal
exit /b 0

:print_hash
set "name=%~1"
set "path_=%~2"
if not exist "%path_%" (
    echo %name%: file not found ^(%path_%^)
    goto :eof
)
for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%path_%').Hash.ToLower()"`) do (
    echo %name%: %%H
)
goto :eof
