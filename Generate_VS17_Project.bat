@echo off

:: Delete intermediate files from the build directory
echo 1. Deleting intermediate, lib and pdb files...
call "Scripts\clean.bat"
cd /D "%~dp0"

:: Copy required assets to the build directory
echo 2. Copying required assets to the build directory...
xcopy "Assets\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I /y
xcopy "Assets\Standard Assets" "Binaries\Release\Standard Assets\" /E /I /y

:: Copy required DLLs to the build directory
echo 3. Copying required DLLs to the build directory...
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I /y

:: Generate VS solution
echo 4. Generating Visual Studio 2017 solution...
cd Scripts
premake5 vs2017