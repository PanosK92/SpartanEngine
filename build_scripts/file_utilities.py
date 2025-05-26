# Copyright(c) 2015-2025 Panos Karabelas
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
install_and_import('tenacity')

import requests
from tqdm import tqdm
from tenacity import retry, stop_after_attempt, wait_exponential, retry_if_exception_type

def calculate_file_hash(file_path):
    hash_func = hashlib.new("sha256")
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_func.update(chunk)
    return hash_func.hexdigest()

def download_file(url, destination, expected_hash, max_retries=3, chunk_size=1024):
    """
    Download a file with retry and resume support.
    
    Args:
        url (str): URL of the file to download.
        destination (str): Local path to save the file.
        expected_hash (str): Expected SHA256 hash of the file.
        max_retries (int): Maximum number of retry attempts.
        chunk_size (int): Size of chunks to read/write.
    """
    # Ensure destination directory exists
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    
    # Check if file exists and get its size
    current_size = 0
    if os.path.exists(destination):
        current_size = os.path.getsize(destination)
        if calculate_file_hash(destination) == expected_hash:
            print(f"File {destination} already exists with the correct hash. Skipping download.")
            return True

    # Get file size from server
    try:
        response = requests.head(url, allow_redirects=True)
        total_size = int(response.headers.get('content-length', 0))
    except requests.RequestException as e:
        print(f"Failed to get file size: {e}")
        return False

    # If file exists but size doesn't match total size, resume download
    headers = {'Range': f'bytes={current_size}-'} if current_size > 0 else {}
    
    @retry(
        stop=stop_after_attempt(max_retries),
        wait=wait_exponential(multiplier=1, min=4, max=10),
        retry=retry_if_exception_type((requests.ConnectionError, requests.Timeout, requests.HTTPError)),
        reraise=True
    )
    def download_with_retry():
        nonlocal current_size
        print(f"\nDownloading {destination} (Total size: {total_size:,} bytes, Starting from: {current_size:,} bytes)...")
        
        # Initialize tqdm progress bar
        t = tqdm(total=total_size, initial=current_size, unit='iB', unit_scale=True)
        
        try:
            # Open connection with stream=True
            with requests.get(url, stream=True, headers=headers, timeout=30) as response:
                response.raise_for_status()  # Raise exception for bad status codes
                
                # Check if server supports range requests
                if current_size > 0 and response.status_code != 206:  # 206 = Partial Content
                    print("Server does not support range requests. Restarting download...")
                    current_size = 0
                    headers.clear()
                    t.reset(total=total_size)
                
                # Open file in append mode if resuming, else write mode
                mode = 'ab' if current_size > 0 else 'wb'
                with open(destination, mode) as f:
                    for chunk in response.iter_content(chunk_size):
                        if chunk:  # Filter out keep-alive chunks
                            f.write(chunk)
                            t.update(len(chunk))
        
        except requests.RequestException as e:
            print(f"Download failed: {e}. Retrying...")
            raise
        finally:
            t.close()
        
        # Verify file size after download
        downloaded_size = os.path.getsize(destination)
        if total_size > 0 and downloaded_size != total_size:
            print(f"Download incomplete: {downloaded_size:,} of {total_size:,} bytes downloaded.")
            raise requests.RequestException("Incomplete download")
        
        # Verify hash
        if calculate_file_hash(destination) != expected_hash:
            print("Hash mismatch. Downloaded file is corrupted.")
            return False
        
        print(f"Successfully downloaded {destination}.")
        return True

    try:
        return download_with_retry()
    except Exception as e:
        print(f"Failed to download {destination} after {max_retries} attempts: {e}")
        return False

def extract_archive(archive_path, destination_path):
    # Check if 7z.exe exists locally
    current_dir_7z  = Path("7z.exe")
    if current_dir_7z.exists():
        seven_zip_exe = current_dir_7z
    else:
        # define the path where 7z.exe should be if not in the current directory
        seven_zip_exe = Path("build_scripts") / "7z.exe"
        seven_zip_exe = seven_zip_exe.resolve()

    # check if the 7z executable exists
    if not os.path.exists(seven_zip_exe):
        raise FileNotFoundError(f"The 7z executable was not found at {seven_zip_exe}. Please check the path or installation.")
    
    archive_path_str = str(Path(archive_path).resolve())
    destination_path_str = str(Path(destination_path).resolve())

    cmd = [str(seven_zip_exe), 'x', archive_path_str, '-o'+destination_path_str, '-aoa']

    print(f"Extracting {archive_path} to {destination_path} using: {seven_zip_exe}")

    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while extracting: {e}")
        print(f"Error output: {e.stderr}")
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