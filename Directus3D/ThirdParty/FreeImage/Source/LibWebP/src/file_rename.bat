: The following file renaming is needed if one want to compile all files 
: using the same output directory, e.g. "Debug\" or "Release\". 
:
: Usage:
: copy all src WebP files into src\, then, run this script to rename files
:

setlocal

: dec\
del /Q .\dec\dec.*.c
pushd "dec\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAdec.%%A"
del /Q "%%~fA"
)
popd

: demux\
del /Q .\demux\demux.*.c
pushd "demux\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAdemux.%%A"
del /Q "%%~fA"
)
popd

: dsp\
del /Q .\dsp\dsp.*.c
pushd "dsp\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAdsp.%%A"
del /Q "%%~fA"
)
popd

: enc\
del /Q .\enc\enc.*.c
pushd "enc\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAenc.%%A"
del /Q "%%~fA"
)
popd

: mux\
del /Q .\mux\mux.*.c
pushd "mux\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAmux.%%A"
del /Q "%%~fA"
)
popd

: utils\
del /Q .\utils\utils.*.c
pushd "utils\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAutils.%%A"
del /Q "%%~fA"
)
popd

: webp\
del /Q .\webp\webp.*.c
pushd "webp\" && for /f "delims=" %%A in ('dir /a-d /b *.c') do (
copy /Y "%%~fA" "%%~dpAwebp.%%A"
del /Q "%%~fA"
)
popd

endlocal

: Makefiles

del /S /Q Makefile.am
del /S /Q *.pc.in

pause -1


