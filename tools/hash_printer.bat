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
for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%path_%').Hash.ToLower()"`) do (
    echo %name%: %%H
)
goto :eof
