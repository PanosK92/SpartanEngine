#Copyright(c) 2016-2024 Panos Karabelas
#
#Permission is hereby granted, free of charge, to any person obtaining a copy
#of this software and associated documentation files (the "Software"), to deal
#in the Software without restriction, including without limitation the rights
#to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
#copies of the Software, and to permit persons to whom the Software is furnished
#to do so, subject to the following conditions :
#
#The above copyright notice and this permission notice shall be included in
#all copies or substantial portions of the Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
#COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import os
import shutil
import sys
import subprocess
from pathlib import Path

paths = {
    "binaries": {
        "data": Path("binaries/data"),
        "models": Path("binaries/project/models"),
        "music": Path("binaries/project/music"),
        "terrain": Path("binaries/project/terrain"),
        "materials": Path("binaries/project/materials"),
    },
    "third_party_libs": {
        "dx": Path("third_party/libraries/dxcompiler.dll"),
        "fmod": Path("third_party/libraries/fmod.dll"),
        "fmod_debug": Path("third_party/libraries/fmodL.dll"),
    },
    "assets": {
        "models": Path("assets/models"),
        "music": Path("assets/music"),
        "terrain": Path("assets/terrain"),
        "materials": Path("assets/materials"),
    },
}


def is_directory(path):
    if not os.path.exists(path):
        return os.path.splitext(path)[1] == ""
    return os.path.isdir(path)


def copy(source, destination):
    if os.path.isfile(source) and is_directory(destination):
        print(f"Copying file \"{source}\" to directory \"{destination}\"...")
        shutil.copy(source, destination)
    elif is_directory(source) and is_directory(destination):
        print(f"Copying directory \"{source}\" to directory \"{destination}\"...")
        if os.path.exists(destination):
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
    else:
        print(f"Error: {source} and {destination} are not compatible.")
        sys.exit(1)


def extract_third_party_dependencies():
    print("1. Extracting third-party dependencies...")
    cmd = (
        "build_scripts\\7z.exe e third_party\\libraries\\libraries.7z -othird_party\\libraries\\ -aoa"
        if sys.argv[1] == "vs2022"
        else "7za e third_party/libraries/libraries.7z -othird_party/libraries/ -aoa"
    )
    os.system(cmd)


def create_binaries_folder():
    print("\n2. Copying required data to the binaries directory..")
    copy("data", paths["binaries"]["data"])


def copy_dlls():
    print("\n3. Copying required DLLs to the binary directory...")
    for lib in paths["third_party_libs"].values():
        copy(lib, Path("binaries"))


def copy_assets():
    print("\n4. Copying some assets to the project directory...")
    for asset_type, asset_path in paths["assets"].items():
        copy(asset_path, paths["binaries"][asset_type])


def generate_project_files():
    print("\n5. Generating project files...")
    cmd = (
        f"build_scripts\\premake5.exe --file=build_scripts\\premake.lua {sys.argv[1]} {sys.argv[2]}"
        if sys.argv[1] == "vs2022"
        else f"premake5 --file=build_scripts/premake.lua {sys.argv[1]} {sys.argv[2]}"
    )
    subprocess.Popen(cmd, shell=True).communicate()


def main():
    extract_third_party_dependencies()
    create_binaries_folder()
    copy_dlls()
    copy_assets()
    generate_project_files()
    sys.exit(0)


if __name__ == "__main__":
    main()
