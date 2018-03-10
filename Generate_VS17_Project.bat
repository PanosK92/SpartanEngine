@echo off

:: Copy required assets to the build directory
xcopy "Assets\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I /y
xcopy "Assets\Standard Assets" "Binaries\Release\Standard Assets\" /E /I /y

:: Copy required DLLs and PDBs to the build directory
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I /y

:: Generation VS solution
cd Build_Scripts
premake5 vs2017