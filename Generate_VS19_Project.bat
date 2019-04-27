@echo off

cd /D "%~dp0"

echo 1. Deleting intermediate folder and lib files (from the binary directory)...
call "Scripts\clean.bat"
echo:

echo 2. Extracting third-party dependencies...
call Scripts\7z.exe e ThirdParty\mvsc141_x64\mvsc141_x64.7z -oThirdParty\mvsc141_x64\ -aoa
echo:

echo 3. Copying required data to the binary directory...
xcopy "Data" "Binaries\Debug\Data\" /E /I /y
xcopy "Data" "Binaries\Release\Data\" /E /I /y
echo:

echo 4. Copying required DLLs to the binary directory...
xcopy "ThirdParty\mvsc141_x64\dxcompiler.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\dxcompiler.dll" "Binaries\Release\" /E /I /y
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I /y
echo:

echo 5. Generating Visual Studio 2019 solution...
Scripts\premake5.exe --file=scripts\premake.lua vs2019