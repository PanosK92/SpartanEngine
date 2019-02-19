@echo off

cd /D "%~dp0"

echo 1. Deleting intermediate folder and lib files (from the binary directory)...
call "Scripts\clean.bat"

echo 2. Copying required assets to the binary directory...
xcopy "Assets\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I /y
xcopy "Assets\Standard Assets" "Binaries\Release\Standard Assets\" /E /I /y

echo 3. Copying required DLLs to the binary directory...
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I /y

echo 4. Generating Visual Studio 2017 solution...
Scripts\premake5.exe --file=scripts\premake.lua vs2017