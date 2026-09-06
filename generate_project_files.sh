#!/usr/bin/env bash
# Copyright(c) 2015-2026 Panos Karabelas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
# copies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

set -e

cd "$(dirname "$0")"

# prefer a native linux premake5 if present, otherwise fall back to wine + .exe
if [ -x "tools/premake5" ]; then
    premake="tools/premake5"
elif command -v premake5 >/dev/null 2>&1; then
    premake="premake5"
elif [ -f "tools/premake5.exe" ] && command -v wine >/dev/null 2>&1; then
    premake="wine tools/premake5.exe"
else
    echo "premake5 not found."
    echo "install premake5, place tools/premake5, or install wine to run tools/premake5.exe"
    exit 1
fi

lua="tools/premake.lua"

# non interactive: generate_project_files.sh <action> <api>, e.g. gmake2 vulkan
if [ -n "$1" ]; then
    $premake --file="$lua" "$@"
    exit $?
fi

cat <<'EOF'
=============================================
         spartan engine project generator
=============================================

  [1] visual studio 2026 - vulkan
  [2] visual studio 2026 - d3d12
  [3] gmake2 - vulkan (linux)
  [0] exit

EOF

read -rp "enter your choice: " choice
case "$choice" in
    1) $premake --file="$lua" vs2026 vulkan ;;
    2) $premake --file="$lua" vs2026 d3d12 ;;
    3) $premake --file="$lua" gmake2 vulkan ;;
    0) exit 0 ;;
    *) echo "invalid choice: $choice"; exit 1 ;;
esac
