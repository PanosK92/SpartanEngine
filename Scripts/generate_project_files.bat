@echo off

:: Make binary output paths
set bin_debug=Binaries\debug_%~2
set bin_release=Binaries\release_%~2

echo 1. Deleting intermediate folder and lib files (from the binary directory)...
call "Scripts\clean.bat"
echo:

echo 2. Extracting third-party dependencies...
call Scripts\7z.exe e ThirdParty\libraries\libraries.7z -oThirdParty\libraries\ -aoa
echo:

echo 3. Copying required data to the binary directory...
xcopy "Data" "%bin_debug%\data\" 	/E /I /y
xcopy "Data" "%bin_release%\data\" 	/E /I /y
echo:

echo 4. Copying required DLLs to the binary directory...
xcopy "ThirdParty\libraries\dxcompiler.dll" "%bin_debug%\" 		/E /I /Q /y
xcopy "ThirdParty\libraries\fmodL64.dll" 	"%bin_debug%\" 		/E /I /Q /y
xcopy "ThirdParty\libraries\dxcompiler.dll" "%bin_release%\" 	/E /I /Q /y
xcopy "ThirdParty\libraries\fmod64.dll" 	"%bin_release%\" 	/E /I /Q /y
echo:

echo 5. Generating Visual Studio 2019 solution...
Scripts\premake5.exe --file=scripts\premake.lua %*

exit /b
