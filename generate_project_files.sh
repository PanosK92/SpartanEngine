#!/usr/bin/env bash
# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

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

# non-interactive: generate_project_files.sh <action> <api>, e.g. gmake vulkan
if [ -n "$1" ]; then
    $premake --file="$lua" "$@"
    exit $?
fi

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-}" != "dumb" ]; then
    C_ACCENT=$'\033[96m'
    C_CYAN=$'\033[36m'
    C_OK=$'\033[92m'
    C_WARN=$'\033[93m'
    C_ERR=$'\033[91m'
    C_TEXT=$'\033[97m'
    C_MUTED=$'\033[90m'
    C_BOLD=$'\033[1m'
    C_RST=$'\033[0m'
else
    C_ACCENT="" C_CYAN="" C_OK="" C_WARN="" C_ERR="" C_TEXT="" C_MUTED="" C_BOLD="" C_RST=""
fi

fast_mode=0

detect_workspace() {
    if [ -d "spartan.xcodeproj" ] || [ -d "spartan.xcworkspace" ]; then
        active_proj="${C_ACCENT}Xcode · Metal${C_RST}"
    elif [ -f "Makefile" ]; then
        active_proj="${C_ACCENT}GNU Make · Vulkan${C_RST}"
    elif [ -f "spartan.vcxproj" ]; then
        if grep -q "API_GRAPHICS_D3D12" spartan.vcxproj 2>/dev/null; then
            active_proj="${C_ACCENT}Visual Studio · DirectX 12${C_RST}"
        elif grep -q "API_GRAPHICS_WEBGPU" spartan.vcxproj 2>/dev/null; then
            active_proj="${C_ACCENT}Visual Studio · WebGPU${C_RST}"
        else
            active_proj="${C_ACCENT}Visual Studio · Vulkan${C_RST}"
        fi
    else
        active_proj="${C_MUTED}none generated yet${C_RST}"
    fi
}

render_menu() {
    clear 2>/dev/null || true
    detect_workspace

    if [ "$fast_mode" -eq 1 ]; then
        fast_badge="${C_WARN}ON${C_RST}  ${C_MUTED}(skipping setup)${C_RST}                    "
    else
        fast_badge="${C_MUTED}OFF${C_RST}                                     "
    fi

    printf "\n"
    printf "  %s%s███████╗██████╗  █████╗ ██████╗ ████████╗ █████╗ ███╗   ██╗%s\n" "$C_ACCENT" "$C_BOLD" "$C_RST"
    printf "  %s%s██╔════╝██╔══██╗██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗████╗  ██║%s\n" "$C_ACCENT" "$C_BOLD" "$C_RST"
    printf "  %s███████╗██████╔╝███████║██████╔╝   ██║   ███████║██╔██╗ ██║%s\n" "$C_ACCENT" "$C_RST"
    printf "  %s╚════██║██╔═══╝ ██╔══██║██╔══██╗   ██║   ██╔══██║██║╚██╗██║%s\n" "$C_CYAN" "$C_RST"
    printf "  %s███████║██║     ██║  ██║██║  ██║   ██║   ██║  ██║██║ ╚████║%s\n" "$C_CYAN" "$C_RST"
    printf "  %s╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝%s\n" "$C_MUTED" "$C_RST"
    printf "  %s%sE N G I N E%s  %s·  P R O J E C T   G E N E R A T O R%s\n\n" "$C_TEXT" "$C_BOLD" "$C_RST" "$C_MUTED" "$C_RST"
    printf "  %sWorkspace%s   %s   %s│  Host%s  %s\n\n" "$C_MUTED" "$C_RST" "$active_proj" "$C_MUTED" "$C_RST" "$(uname -s)"

    printf "  %s╭────────────────────────────────────────────────────────────────────────╮%s\n" "$C_CYAN" "$C_RST"
    printf "  %s│%s  %sSELECT TARGET%s                                                         %s│%s\n" "$C_CYAN" "$C_RST" "$C_MUTED" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s├────────────────────────────────────────────────────────────────────────┤%s\n" "$C_CYAN" "$C_RST"
    printf "  %s│%s                                                                        %s│%s\n" "$C_CYAN" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[1]%s  %s%sVisual Studio 2026 · DirectX 12%s   %srecommended (Windows)%s         %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_ACCENT" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_OK" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[2]%s  %s%sVisual Studio 2026 · Vulkan%s                                     %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_ACCENT" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[3]%s  %s%sGNU Make · Vulkan%s                 %srecommended (Linux)%s           %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_ACCENT" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_OK" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[4]%s  %s%sXcode · Metal%s                     %srecommended (macOS)%s           %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_ACCENT" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_OK" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s                                                                        %s│%s\n" "$C_CYAN" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s├────────────────────────────────────────────────────────────────────────┤%s\n" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[S]%s  %sSync dependencies only%s  %sStage data & runtime libraries%s          %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_TEXT" "$C_RST" "$C_MUTED" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[F]%s  %sFast regenerate%s         %s%s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_TEXT" "$C_RST" "$C_TEXT" "$C_RST" "$fast_badge" "$C_CYAN" "$C_RST"
    printf "  %s│%s   %s%s[0]%s  %sExit%s                                                            %s│%s\n" "$C_CYAN" "$C_RST" "$C_BOLD" "$C_MUTED" "$C_RST" "$C_MUTED" "$C_RST" "$C_CYAN" "$C_RST"
    printf "  %s╰────────────────────────────────────────────────────────────────────────╯%s\n\n" "$C_CYAN" "$C_RST"
    printf "  %s▸%s %sPress a key %s[1 / 2 / 3 / 4 / S / F / 0]%s: " "$C_ACCENT" "$C_RST" "$C_TEXT" "$C_MUTED" "$C_RST"
}

while true; do
    render_menu
    read -rsn1 choice || exit 0
    printf "\n"

    extra=()
    if [ "$fast_mode" -eq 1 ]; then
        extra+=(--skip-setup)
    fi

    case "$choice" in
        1) args=(vs2026 d3d12 "${extra[@]}") ;;
        2) args=(vs2026 vulkan "${extra[@]}") ;;
        3) args=(gmake vulkan "${extra[@]}") ;;
        4) args=(xcode4 metal "${extra[@]}") ;;
        s|S) args=(setup) ;;
        f|F)
            fast_mode=$((1 - fast_mode))
            continue
            ;;
        0|q|Q) exit 0 ;;
        *) continue ;;
    esac

    clear 2>/dev/null || true
    set +e
    $premake --file="$lua" "${args[@]}"
    rc=$?
    set -e

    if [ "$rc" -eq 0 ]; then
        printf "\n  %s╭────────────────────────────────────────────────────────────────────────╮%s\n" "$C_OK" "$C_RST"
        printf "  %s│%s  %s%s✓  Project generation complete%s                                         %s│%s\n" "$C_OK" "$C_RST" "$C_BOLD" "$C_OK" "$C_RST" "$C_OK" "$C_RST"
        printf "  %s╰────────────────────────────────────────────────────────────────────────╯%s\n\n" "$C_OK" "$C_RST"
    else
        printf "\n  %s╭────────────────────────────────────────────────────────────────────────╮%s\n" "$C_ERR" "$C_RST"
        printf "  %s│%s  %s%s✗  Project generation failed%s   %s(exit code %d)%s                           %s│%s\n" "$C_ERR" "$C_RST" "$C_BOLD" "$C_ERR" "$C_RST" "$C_MUTED" "$rc" "$C_RST" "$C_ERR" "$C_RST"
        printf "  %s╰────────────────────────────────────────────────────────────────────────╯%s\n\n" "$C_ERR" "$C_RST"
    fi

    printf "  %s▸%s %sPress %s[R]%s%s for menu or %s[Q]%s%s to exit%s\n" "$C_ACCENT" "$C_RST" "$C_TEXT" "$C_BOLD" "$C_RST" "$C_TEXT" "$C_BOLD" "$C_RST" "$C_TEXT" "$C_RST"
    read -rsn1 post || exit "$rc"
    case "$post" in
        r|R) continue ;;
        *) exit "$rc" ;;
    esac
done
