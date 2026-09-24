@echo off
rem Copyright(c) 2015-2026 Panos Karabelas
rem Licensed under the Spartan Engine License. See license.md in the repository root.
rem https://github.com/PanosK92/SpartanEngine/blob/master/license.md
rem Commercial use requires written permission and negotiated payment terms.

setlocal enableextensions
pushd "%~dp0\.."

set "libraries=%USERPROFILE%\Dropbox\libraries.7z"
set "project=%USERPROFILE%\Dropbox\project.7z"

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
for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $stream=[IO.File]::OpenRead($env:path_); $sha=[Security.Cryptography.SHA256]::Create(); try { [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose(); $stream.Dispose() }"`) do (
    echo %name%: %%H
)
goto :eof
