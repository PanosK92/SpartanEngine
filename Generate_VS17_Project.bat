@echo off
:: Copy required assets to build directory
xcopy "Runtime\Assets" "Binaries\Release\Standard Assets\" /E /I
:: Generation VS solution
cd Build_Scripts
premake5 vs2017
pause