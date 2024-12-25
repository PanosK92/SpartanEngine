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

import sys
import subprocess
import importlib
import platform

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

import os
import hashlib
from tqdm import tqdm
import requests

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
    # Determine the path to 7z based on the use_working_dir flag
    seven_zip_exe = '7z'
    if use_working_dir:
        seven_zip_exe = '7z.exe' if is_windows else '7za'
    else:
        exe_dir = os.path.join(os.getcwd(), 'build_scripts')
        if is_windows:
            seven_zip_exe = os.path.join(exe_dir, '7z.exe')
        elif platform.system() == 'Linux':
            seven_zip_exe = os.path.join(exe_dir, '7za')
        elif platform.system() == "FreeBSD":
            seven_zip_exe = os.path.join(exe_dir, '/usr/local/bin/7z')
        else:
            print("Unsupported platform!")
            return

    # Construct the command
    cmd = f"{seven_zip_exe} x {archive_path} -o{destination_path}"

    print(f"Extracting {archive_path} to {destination_path} using: {seven_zip_exe}")

    try:
        # Execute the command
        result = subprocess.run(cmd, check=True, shell=True, text=True, capture_output=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while extracting: {e}")
        print(f"Error output: {e.stderr}")
        raise  # Re-raise the exception for higher-level error handling if needed
    except FileNotFoundError:
        print(f"The 7z executable was not found at {seven_zip_exe}. Please check the path or installation.")
        raise
