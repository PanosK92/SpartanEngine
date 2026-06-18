#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

premake="tools/premake5"
lua="tools/premake.lua"

run_choice() {
    case "$1" in
        1) "$premake" --file="$lua" vs2026 vulkan ;;
        2) "$premake" --file="$lua" vs2026 d3d12 ;;
        3) "$premake" --file="$lua" gmake2 vulkan ;;
        0) exit 0 ;;
        *) echo "invalid choice: $1"; exit 1 ;;
    esac
}

if [ -n "$1" ]; then
    run_choice "$1"
    exit 0
fi

cat <<'EOF'
=============================================
         spartan engine project generator
=============================================

  [1] visual studio 2026 - vulkan
  [2] visual studio 2026 - d3d12 (wip)
  [3] gmake2 - vulkan (linux)
  [0] exit

EOF

read -rp "enter your choice: " choice
run_choice "$choice"
