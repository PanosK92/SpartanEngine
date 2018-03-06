@echo off
:: Copy required assets to build directory
xcopy "Assets\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I
xcopy "Assets\Standard Assets" "Binaries\Release\Standard Assets\" /E /I
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I
:: Generation VS solution
cd Build_Scripts
premake5 vs2017
pause