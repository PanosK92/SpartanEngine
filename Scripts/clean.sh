#!/bin/bash

# delete obj directory
rm -rf "Binaries/Obj/$1"

# delete exe directory files (that don't need to be part of build artifacts)
rm -f "Binaries/$1/*.exp"
rm -f "Binaries/$1/*.ilk"
rm -f "Binaries/$1/*.lib"
rm -f "Binaries/$1/*.pdb"

# delete debug dlls (in case they exist)
rm -rf "Binaries/$1/fmodL64.dll"