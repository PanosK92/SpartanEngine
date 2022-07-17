#!/bin/bash

# Deduce OS we are running on
is_windows=true
if [ "$1" = "gmake2" ]; then
    is_windows=false
fi

# Extract third-party libraries (that the project will link to)
echo
echo "= 1. Extracting third-party dependencies... ==========================================="
if [ "$is_windows" = false ]; then
	 7za e third_party/libraries/libraries.7z -othird_party/libraries/ -aoa
else 
	 build_scripts/7z.exe e third_party/libraries/libraries.7z -othird_party/libraries/ -aoa
fi
echo "======================================================================================="

# Copy engine data to the binary directory
echo
echo "2. Copying required data to the binary directory..."
mkdir -p binaries/
cp -r Data binaries

# Copy engine DLLs to the binary directory
echo
echo "3. Copying required DLLs to the binary directory..."
cp third_party/libraries/dxcompiler.dll binaries/
cp third_party/libraries/fmod64.dll binaries/
cp third_party/libraries/fmodL64.dll binaries/

# Copy some 3D models into the project folder
echo
echo "4. Creating project directory and copying some assets..."
mkdir -p binaries/project
cp -r assets/models binaries/project
cp -r assets/environment binaries/project

# Generate project files
echo
if [ "$is_windows" = false ]; then
	echo "5. Generating MakeFiles..."
	build_scripts/premake5 --file=build_scripts/premake.lua $@
else
	echo "5. Generating Visual Studio solution..."
	build_scripts/premake5 --file=build_scripts/premake.lua $@
fi