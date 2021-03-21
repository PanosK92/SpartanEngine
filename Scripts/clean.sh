#!/bin/bash

# delete intermediate directory
rm -rf Binaries/intermediate

# delete binary directory files (that don't need to be part of build artifacts)
rm -f Binaries/Debug/*.exp
rm -f Binaries/Debug/*.ilk
rm -f Binaries/Debug/*.lib
rm -f Binaries/Debug/*.pdb
rm -f Binaries/Release/*.exp
rm -f Binaries/Release/*.ilk
rm -f Binaries/Release/*.lib
rm -f Binaries/Release/*.pdb