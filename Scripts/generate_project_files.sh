#!/bin/bash

echo "1. Deleting intermediate folder and lib files (from the binary directory)..."
./Scripts/clean.sh

echo "2. Extracting third-party dependencies..."
./Scripts/7za e ThirdParty/libraries/libraries.7z -oThirdParty/libraries/ -aoa

echo "3. Copying required data to the binary directory..."
mkdir -p Binaries/Debug/Data
cp -R Data Binaries/Debug/Data
mkdir -p Binaries/Release/Data
cp -R Data Binaries/Release/Data

echo "4. Copying required DLLs to the binary directory..."
cp ThirdParty/libraries/dxcompiler.dll Binaries/Debug/
cp ThirdParty/libraries/fmodL64.dll	Binaries/Debug/
cp ThirdParty/libraries/dxcompiler.dll Binaries/Release/
cp ThirdParty/libraries/fmod64.dll Binaries/Release/

echo "5. Generating MakeFiles..."
./Scripts/premake5 --file=Scripts/premake.lua $@
