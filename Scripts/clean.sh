#!/bin/bash

# delete intermediate directory
rm -rf Binaries/intermediate

# delete binary directory files (that don't need to be part of build artifacts)
rm -f Binaries/*.exp
rm -f Binaries/*.ilk
rm -f Binaries/*.lib
rm -f Binaries/*.pdb