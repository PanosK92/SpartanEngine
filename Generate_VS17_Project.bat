@echo off
:: Copy required assets to build directory
xcopy "Prerequisites\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I
xcopy "Prerequisites\Standard Assets" "Binaries\Release\Standard Assets\" /E /I
xcopy "Prerequisites\DLLs\fmodL64.dll" "Binaries\Debug\" /E /I
xcopy "Prerequisites\DLLs\fmod64.dll" "Binaries\Release\" /E /I
:: Generation VS solution
cd Build_Scripts
premake5 vs2017
pause