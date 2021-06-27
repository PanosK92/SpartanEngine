#!/bin/bash

# delete obj directory
rm -rf Binaries/Obj

# delete exe directory files (that don't need to be part of build artifacts)
rm -f Binaries/*.exp
rm -f Binaries/*.ilk
rm -f Binaries/*.lib
rm -f Binaries/*.pdb

# delete debug dlls (in case they exist)
rm -rf Binaries/fmodL64.dll