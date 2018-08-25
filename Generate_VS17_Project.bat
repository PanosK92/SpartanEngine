@echo off

:: Delete.obj files from the build directory
echo 1. Deleting .obj files from the build directory...
@RD /S /Q "Binaries\Obj"

:: Copy required assets to the build directory
echo 2. Copying required assets to the build directory...
xcopy "Assets\Standard Assets" "Binaries\Debug\Standard Assets\" /E /I /y
xcopy "Assets\Standard Assets" "Binaries\Release\Standard Assets\" /E /I /y

:: Copy required DLLs and PDBs to the build directory
echo 3. Copying required DLLs and PDBs to the build directory...
xcopy "ThirdParty\mvsc141_x64\fmodL64.dll" "Binaries\Debug\" /E /I /Q /y
xcopy "ThirdParty\mvsc141_x64\fmod64.dll" "Binaries\Release\" /E /I /y

:: Generation VS solution
echo 4. Generating Visual Studio 2017 solution...
cd Build_Scripts
premake5 vs2017