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

import hashlib
import importlib
import os
import requests
import shutil
import stat
import subprocess
import sys
from pathlib import Path

def install_and_import(package):
    try:
        importlib.import_module(package)
    except ImportError:
        print(f"{package} not installed. Installing now...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])
    finally:
        globals()[package] = importlib.import_module(package)

install_and_import('tqdm')
install_and_import('requests')

def calculate_file_hash(file_path):
    hash_func = hashlib.new("sha256")
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_func.update(chunk)
    return hash_func.hexdigest()

def download_file(url, destination, expected_hash):
    if os.path.exists(destination):
        if calculate_file_hash(destination) == expected_hash:
            print(f"File {destination} already exists with the correct hash. Skipping download.")
            return

    print(f"\nDownloading {destination}...")
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    response = requests.get(url, stream=True)
    total_size = int(response.headers.get('content-length', 0))
    block_size = 1024
    from tqdm import tqdm
    t = tqdm(total=total_size, unit='iB', unit_scale=True)
    
    with open(destination, 'wb') as f:
        for chunk in response.iter_content(block_size):
            t.update(len(chunk))
            f.write(chunk)
    t.close()
    
    if total_size != 0 and t.n != total_size:
        print("ERROR, something went wrong during download")
        return
    
    if calculate_file_hash(destination) != expected_hash:
        print(f"ERROR, hash mismatch for {destination}")
        return

def extract_archive(archive_path, destination_path, is_windows, use_working_dir=False):
    # determine the path to 7z based on the use_working_dir flag
    seven_zip_exe = Path("build_scripts") / ("7z.exe" if is_windows else "7za")
    seven_zip_exe = seven_zip_exe.resolve() if use_working_dir else seven_zip_exe
    
    # convert paths to string for subprocess, ensuring they are quoted if they contain spaces
    archive_path_str = f'"{Path(archive_path).resolve()}"'
    destination_path_str = f'"{Path(destination_path).resolve()}"'

    # construct the command as a string with quoted paths
    cmd = f'{str(seven_zip_exe)} x {archive_path_str} -o{destination_path_str} -aoa'

    print(f"Extracting {archive_path} to {destination_path} using: {seven_zip_exe}")

    try:
        # execute the command with shell=True to handle paths with spaces
        result = subprocess.run(cmd, check=True, shell=True, capture_output=True, text=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while extracting: {e}")
        print(f"Error output: {e.stderr}")
        raise  # re-raise the exception for higher-level error handling if needed
    except FileNotFoundError:
        print(f"The 7z executable was not found at {seven_zip_exe}. Please check the path or installation.")
        raise

def copy(source, destination):
    def on_rm_error(func, path, exc_info):
        os.chmod(path, stat.S_IWRITE)
        func(path)

    source_path = Path(source).resolve()
    dest_path = Path(destination).resolve()

    # check if source is a directory or file
    if source_path.is_dir():
        # if source is a directory, ensure destination is a directory too
        dest_path.mkdir(parents=True, exist_ok=True)  # Create the destination directory if it doesn't exist
        print(f"Copying directory \"{source_path}\" to directory \"{dest_path}\"...")
        shutil.rmtree(str(dest_path), onerror=on_rm_error)
        shutil.copytree(str(source_path), str(dest_path), dirs_exist_ok=True)
    elif source_path.is_file():
        # if source is a file, ensure the parent directory of the destination exists
        dest_path.parent.mkdir(parents=True, exist_ok=True)  # Create parent directory if it doesn't exist
        target = dest_path if dest_path.is_file() else dest_path / source_path.name
        print(f"Copying file \"{source_path}\" to \"{target}\"...")
        shutil.copy2(str(source_path), str(target))
    else:
        print(f"Error: Source '{source_path}' is neither a file nor a directory.")
        return False
    return True