#!/bin/bash

# delete obj directory
rm -rf Binaries/Obj

# delete exe directory files (that don't need to be part of build artifacts)
rm -f Binaries/Exe/*.exp
rm -f Binaries/Exe/*.ilk
rm -f Binaries/Exe/*.lib
rm -f Binaries/Exe/*.pdb