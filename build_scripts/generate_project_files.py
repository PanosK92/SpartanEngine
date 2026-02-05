# Copyright(c) 2016-2025 Panos Karabelas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is furnished
# to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

import file_utilities

paths = {
    "binaries": {
        "data": Path("binaries") / "data",
        "models": Path("binaries") / "project" / "models",
        "music": Path("binaries") / "project" / "music",
        "terrain": Path("binaries") / "project" / "terrain",
        "materials": Path("binaries") / "project" / "materials",
    },
    "third_party_libs": {
        "dx": Path("third_party") / "libraries" / "dxcompiler.dll",
        "xess": Path("third_party") / "libraries" / "libxess.dll",
    },
    "assets": {
        "models": Path("assets") / "models",
        "music": Path("assets") / "music",
        "terrain": Path("assets") / "terrain",
        "materials": Path("assets") / "materials",
    },
}

def generate_project_files():
    # Provide default arguments if not specified
    if len(sys.argv) < 3:
        if os.name == 'nt':
            sys.argv.extend(["vs2022", "windows"])
        else:
            # Default to gmake2 for Linux (can also use "cmake")
            sys.argv.extend(["gmake2", "vulkan"])

    # determine if we're using Windows or another platform
    action = sys.argv[1].strip('"')
    is_windows = action.startswith("vs")  # Assuming 'vs' prefix for Visual Studio

    # determine premake executable
    if is_windows:
        premake_exe = Path.cwd() / "build_scripts" / "premake5.exe"
    else:
        # On Linux/macOS, check if premake5 is in the PATH
        premake_exe = shutil.which("premake5")
        if not premake_exe:
             # Fallback to local script folder check
             premake_exe = Path.cwd() / "build_scripts" / "premake5"

        if not premake_exe or (isinstance(premake_exe, Path) and not premake_exe.exists()):
             raise FileNotFoundError("premake5 executable not found in PATH or build_scripts/.")

    premake_lua = Path("build_scripts") / "premake.lua"

    # remove quotes if they exist around sys.argv[2] (platform argument)
    platform = sys.argv[2].strip('"')

    # construct the command as a string with quoted paths
    cmd = f'"{str(premake_exe)}" --file="{str(premake_lua)}" "{action}" "{platform}"'

    print("Running command:", cmd)

    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(result.stderr)
        if result.returncode != 0:
            print(f"\nPremake failed with exit code {result.returncode}")
            sys.exit(1)

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

def print_usage():
    """Print usage information"""
    print("\nUsage: python generate_project_files.py [action] [platform]")
    print("\nActions:")
    print("  vs2022         - Generate Visual Studio 2022 project files (Windows)")
    print("  cmake          - Generate CMake project files (Linux/Windows)")
    print("  gmake2         - Generate GNU Make project files (Linux, default)")
    print("\nPlatforms:")
    print("  windows        - Windows platform")
    print("  linux          - Linux platform")
    print("  vulkan         - Vulkan graphics API (Linux, default)")
    print("\nExamples:")
    print("  python generate_project_files.py          # Use defaults (gmake2 vulkan on Linux)")
    print("  python generate_project_files.py cmake vulkan")
    print("  python generate_project_files.py gmake2 linux")
    print("  python generate_project_files.py vs2022 windows")
    print()

def main():
    is_ci = "ci" in sys.argv

    if "--help" in sys.argv or "-h" in sys.argv:
        print_usage()
        sys.exit(0)

    print("\n1. Create binaries folder with the required data files...\n")
    file_utilities.copy("data", paths["binaries"]["data"])
    file_utilities.copy(Path("build_scripts") / "file_utilities.py", "binaries")

    if os.name == 'nt':
        file_utilities.copy(Path("build_scripts") / "7z.exe", "binaries")
        file_utilities.copy(Path("build_scripts") / "7z.dll", "binaries")

    print("\n2. Download and extract libraries...")
    library_url           = 'https://www.dropbox.com/scl/fi/p4c3nxx89xjdd5letdblw/libraries.7z?rlkey=i71b8403gjvv8t0l5nox1knsg&st=br54pnqf&dl=1'
    library_expected_hash = '01e7978852c3d2f6925423e540d97ba3ef3734f7094d12b78aacbc3852b7d6dd'
    library_destination   = Path("third_party") / "libraries" / "libraries.7z"
    file_utilities.download_file(library_url, str(library_destination), library_expected_hash)
    file_utilities.extract_archive(str(library_destination), str(Path("third_party") / "libraries"))

    print("3. Copying required DLLs to the binary directory...")
    if os.name == 'nt':
        for lib in paths["third_party_libs"].values():
            file_utilities.copy(lib, Path("binaries"))
    else:
        print("Skipping DLL copy on non-Windows platform.")

    print("\n4. Generate project files...\n")
    generate_project_files()

    if not is_ci:
        input("\nPress any key to continue...")

    sys.exit(0)

if __name__ == "__main__":
    main()