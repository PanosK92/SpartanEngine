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
    # determine if we're using Windows or another platform
    is_windows = sys.argv[1].startswith("vs")  # Assuming 'vs' prefix for Visual Studio

    # construct the command, stripping any surrounding quotes from arguments
    premake_exe = Path.cwd() / "build_scripts" / ("premake5.exe" if is_windows else "premake5")
    premake_lua = Path("build_scripts") / "premake.lua"

    # remove quotes if they exist around sys.argv[1] and sys.argv[2]
    action = sys.argv[1].strip('"')
    platform = sys.argv[2].strip('"')

    # construct the command as a string with quoted paths
    cmd = f'"{str(premake_exe)}" --file="{str(premake_lua)}" "{action}" "{platform}"'

    print("Running command:", cmd)
    
    try:
        result = subprocess.run(cmd, shell=True, check=True, capture_output=True, text=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Error occurred while generating project files: {e}")
        print(f"Error output: {e.stderr}")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

def main():
    is_ci = "ci" in sys.argv
    
    print("\n1. Create binaries folder with the required data files...\n")
    file_utilities.copy("data", paths["binaries"]["data"])
    file_utilities.copy(Path("build_scripts") / "download_assets.py", "binaries")
    file_utilities.copy(Path("build_scripts") / "file_utilities.py", "binaries")
    file_utilities.copy(Path("build_scripts") / "7z.exe", "binaries")
    file_utilities.copy(Path("build_scripts") / "7z.dll", "binaries")

    print("\n2. Download and extract libraries...")
    library_url           = 'https://www.dropbox.com/scl/fi/0ubcbz5q0uvws0ft52y2h/libraries.7z?rlkey=2sj1kruslborvqcvnsnjnu0c8&st=509i7lm2&dl=1'
    library_expected_hash = 'afa5f40c77b3b124f4411d9d78374e1e5a60f1281b9cd2f14c92e62109879e4e'
    library_destination   = Path("third_party") / "libraries" / "libraries.7z"
    file_utilities.download_file(library_url, str(library_destination), library_expected_hash)
    file_utilities.extract_archive(str(library_destination), str(Path("third_party") / "libraries"))
    
    print("3. Copying required DLLs to the binary directory...")
    for lib in paths["third_party_libs"].values():
        file_utilities.copy(lib, Path("binaries"))

    print("\n4. Generate project files...\n")
    generate_project_files()
    
    if not is_ci:
        input("\nPress any key to continue...")
        
    sys.exit(0)

if __name__ == "__main__":
    main()