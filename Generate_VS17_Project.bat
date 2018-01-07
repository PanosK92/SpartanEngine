@echo off
xcopy "Runtime\Assets" "Binaries\Release\Standard Assets\" /E /I
cd Build_Scripts
premake5 vs2017
pause