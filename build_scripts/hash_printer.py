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
from pathlib import Path
import file_utilities

def print_local_file_hashes():
    local_files = {
        'libraries': '../third_party/libraries/libraries.7z',
        'project': '../binaries/project/project.7z'
    }
    
    print("Local file hashes:")
    for name, path in local_files.items():
        if os.path.exists(path):
            hash = file_utilities.calculate_file_hash(path)
            print(f"{name}: {hash}")
        else:
            print(f"{name}: File not found")
    
    # wait for user input to give them time to read the hashes
    input("\nPress Enter to continue...")

if __name__ == "__main__":
    print_local_file_hashes()