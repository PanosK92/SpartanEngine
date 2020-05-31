@echo off

echo 1. Deleting intermediate folder and lib files (from the binary directory)...
call "Scripts\clean.bat"
echo:

echo 2. Extracting third-party dependencies...
call Scripts\7z.exe e ThirdParty\libraries\libraries.7z -oThirdParty\libraries\ -aoa
echo:

echo 3. Copying required data to the binary directory...
xcopy "Data" "Binaries\Debug\Data\" 	/E /I /y
xcopy "Data" "Binaries\Release\Data\"	/E /I /y
echo:

echo 4. Copying required DLLs to the binary directory...
xcopy "ThirdParty\libraries\dxcompiler.dll" "Binaries\Debug\" 		/E /I /Q /y
xcopy "ThirdParty\libraries\fmodL64.dll" 	"Binaries\Debug\"		/E /I /Q /y
xcopy "ThirdParty\libraries\dxcompiler.dll" "Binaries\Release\" 	/E /I /Q /y
xcopy "ThirdParty\libraries\fmod64.dll" 	"Binaries\Release\" 	/E /I /Q /y
echo:

echo 5. Generating Visual Studio solution...
Scripts\premake5.exe --file=scripts\premake.lua %*

exit /b
